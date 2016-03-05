/*
 * Created by Ivo Georgiev on 2/9/16.
 */
/*
 * Forked on 2/21
 */

#include <stdlib.h>
#include <assert.h>
//#include <w32api/rpcndr.h>
#include <stdio.h> // for perror()

#include "mem_pool.h"

/*************/
/*           */
/* Constants */
/*           */
/*************/
static const float      MEM_FILL_FACTOR                 = 0.75;
static const unsigned   MEM_EXPAND_FACTOR               = 2;

static const unsigned   MEM_POOL_STORE_INIT_CAPACITY    = 20;
static const float      MEM_POOL_STORE_FILL_FACTOR      = 0.75; //MEM_FILL_FACTOR;
static const unsigned   MEM_POOL_STORE_EXPAND_FACTOR    = 2;    //MEM_EXPAND_FACTOR;

static const unsigned   MEM_NODE_HEAP_INIT_CAPACITY     = 40;
static const float      MEM_NODE_HEAP_FILL_FACTOR       = .75; //MEM_FILL_FACTOR;
static const unsigned   MEM_NODE_HEAP_EXPAND_FACTOR     = 2;    //MEM_EXPAND_FACTOR;

static const unsigned   MEM_GAP_IX_INIT_CAPACITY        = 40;
static const float      MEM_GAP_IX_FILL_FACTOR          = 0.75; //MEM_FILL_FACTOR;
static const unsigned   MEM_GAP_IX_EXPAND_FACTOR        = 2;    //MEM_EXPAND_FACTOR;



/*********************/
/*                   */
/* Type declarations */
/*                   */
/*********************/
typedef struct _node {
    alloc_t alloc_record;
    unsigned used;
    unsigned allocated;
    struct _node *next, *prev; // doubly-linked list for gap deletion
} node_t, *node_pt;

typedef struct _gap {
    size_t size;
    node_pt node;
} gap_t, *gap_pt;

typedef struct _pool_mgr {
    pool_t pool;
    node_pt node_heap;
    unsigned total_nodes;
    unsigned used_nodes;
    gap_pt gap_ix;
    unsigned gap_ix_capacity;
} pool_mgr_t, *pool_mgr_pt;



/***************************/
/*                         */
/* Static global variables */
/*                         */
/***************************/
static pool_mgr_pt *pool_store = NULL; // an array of pointers, only expand
static unsigned pool_store_size = 0;
static unsigned pool_store_capacity = 0;



/********************************************/
/*                                          */
/* Forward declarations of static functions */
/*                                          */
/********************************************/
static alloc_status _mem_resize_pool_store();
static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr);
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr);
static alloc_status
        _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                           size_t size,
                           node_pt node);
static alloc_status
        _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                size_t size,
                                node_pt node);
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr);
static void insert_node_heap(node_pt first_node, node_pt insert_node);


/****************************************/
/*                                      */
/* Definitions of user-facing functions */
/*                                      */
/****************************************/
alloc_status mem_init() {

    if (pool_store == NULL){
        pool_store = (pool_mgr_pt*) calloc(MEM_POOL_STORE_INIT_CAPACITY, sizeof(pool_mgr_pt));
        pool_store_size = 0;
        pool_store_capacity = MEM_POOL_STORE_INIT_CAPACITY;
        return ALLOC_OK;
    }
    else{
        printf("failed mem_init\n");
        return ALLOC_FAIL;
    }

    // ensure that it's called only once until mem_free
    // allocate the pool store with initial capacity
    // note: holds pointers only, other functions to allocate/deallocate
}

alloc_status mem_free() {
    // ensure that it's called only once for each mem_init
    // make sure all pool managers have been deallocated
    // can free the pool store array
    // update static variables

    return ALLOC_FAIL;
}

pool_pt mem_pool_open(size_t mem_pool_size, alloc_policy policy) {

    // make sure there the pool store is allocated
    if (pool_store == NULL){
        printf("pool store not open\n");
        return NULL;
    }
    // expand the pool store, if necessary
    _mem_resize_pool_store();

    // allocate a new mem pool mgr
    pool_mgr_pt new_pool_mgr = (pool_mgr_pt) malloc(sizeof(pool_mgr_t));
    // check success, on error return null
    // any other cases which would fail?
    if (new_pool_mgr == NULL){
        return NULL;
    }

    // initialize pool memory block, check success, on error deallocate mgr and return null
    char* new_mem_pool = (char*) malloc(sizeof(mem_pool_size));

    if (new_mem_pool == NULL){
        free(new_pool_mgr);
        return NULL;
    }

    // allocate a new node heap
    node_pt new_node_heap = (node_pt) calloc(MEM_NODE_HEAP_INIT_CAPACITY, sizeof(node_t));

    // check success, on error deallocate mgr/pool and return null
    if (new_node_heap == NULL){
        free(new_pool_mgr->pool.mem);
        free(new_pool_mgr);
        return NULL;
    }

    // allocate a new gap index
    gap_pt new_gap_index = (gap_pt) calloc(MEM_GAP_IX_INIT_CAPACITY, sizeof(gap_t));
    // check success, on error deallocate mgr/pool/heap and return null
    if (new_gap_index == NULL){
        free(new_pool_mgr->pool.mem);
        free(new_pool_mgr);
        free(new_node_heap);
        return NULL;
    }

    // assign all the pointers and update meta data:
    //   initialize top node of node heap
    new_node_heap[0].alloc_record.size = mem_pool_size;
    new_node_heap[0].alloc_record.mem = new_mem_pool;
    new_node_heap[0].used = 1;
    new_node_heap[0].allocated = 0;
    new_node_heap[0].next = NULL;
    new_node_heap[0].prev = NULL;

    //   initialize top node of gap index
    new_gap_index[0].size = mem_pool_size;
    new_gap_index[0].node = new_node_heap;

    //   initialize pool mgr pool
    new_pool_mgr->pool.mem = new_mem_pool;
    new_pool_mgr->pool.total_size = mem_pool_size;
    new_pool_mgr->pool.alloc_size = 0;
    new_pool_mgr->pool.policy = policy;
    new_pool_mgr->pool.num_allocs = 0;
    new_pool_mgr->pool.num_gaps = 1;

    // initialize pool mgr
    new_pool_mgr->node_heap = new_node_heap;
    new_pool_mgr->total_nodes = 1;
    new_pool_mgr->used_nodes = 0;
    new_pool_mgr->gap_ix = new_gap_index;
    new_pool_mgr->gap_ix_capacity = MEM_GAP_IX_INIT_CAPACITY;

    //   link pool mgr to pool store
    pool_store[pool_store_size] = new_pool_mgr;

    printf("%d\n", new_mem_pool);
    printf("%d\n", new_pool_mgr->pool.mem);
    printf("%d\n", new_pool_mgr);
    printf("%d\n", &new_pool_mgr->pool);

    printf("%d\n", new_pool_mgr->gap_ix->size);
    printf("%d\n", mem_pool_size);

    //assert(sizeof(new_pool_mgr->pool.mem) == mem_pool_size);
    assert(((int)new_pool_mgr->gap_ix->size) == ((int)mem_pool_size));
    // return the address of the mgr, cast to (pool_pt)
    return (pool_pt)new_pool_mgr;
}

alloc_status mem_pool_close(pool_pt pool) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    // check if this pool is allocated
    // check if pool has only one gap
    // check if it has zero allocations
    // free memory pool
    // free node heap
    // free gap index
    // find mgr in pool store and set to null
    // note: don't decrement pool_store_size, because it only grows
    // free mgr

    return ALLOC_FAIL;
}

alloc_pt mem_new_alloc(pool_pt pool, size_t req_size) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;
    // check if any gaps, return null if none
    if (pool_mgr->gap_ix->size == 0){
        return NULL;
    }
    // expand heap node, if necessary, quit on error
    alloc_status status =_mem_resize_node_heap(pool_mgr);
    assert(status != ALLOC_FAIL);

    // Find a large enough node for allocation:
    // if FIRST_FIT, then find the first sufficient node in the node heap
    node_pt alloc_node = NULL;
    if (pool->policy == FIRST_FIT){
        node_pt current_node = pool_mgr->node_heap;
        while (current_node != NULL){
            if (current_node->used == 1 && current_node->allocated == 0
                && current_node->alloc_record.size >= req_size){
                alloc_node = current_node;
                current_node = NULL;
            }
            else{
                current_node = current_node->next;
            }
        }
    }
    // if BEST_FIT, then find the first sufficient node in the gap index



    // check if node found
    assert(alloc_node != NULL);

    // update metadata (num_allocs, alloc_size)
    pool->num_allocs++;
    pool->alloc_size += req_size;

    // calculate the size of the remaining gap, if any
    size_t new_gap_size = alloc_node->alloc_record.size - req_size;


    // If req alloc is exactly the same size as gap simply convert
    // to gap node to an alloc node and remove the gap index
    if (new_gap_size == 0){
        alloc_node->allocated = 1;
        _mem_remove_from_gap_ix(pool_mgr, alloc_node->alloc_record.size, alloc_node);

    }
    else{
        // Find an unused node in heap
        node_pt new_gap_node = NULL;
        node_pt current_node = pool_mgr->node_heap;
        while (current_node != NULL){
            if (current_node->used == 0){
                new_gap_node = current_node;
                current_node = NULL;
            }
            else{
                current_node = current_node->next;
            }
        }
        assert(new_gap_node != NULL);

        // update alloc records for new alloc & remove old gap from gap index
        alloc_node->allocated = 1;
        alloc_node->alloc_record.size = req_size;
        _mem_remove_from_gap_ix(pool_mgr, alloc_node->alloc_record.size, alloc_node);
        // update alloc records for new gap & insert into gap index
        new_gap_node->alloc_record.mem = alloc_node->alloc_record.mem + req_size;
        new_gap_node->alloc_record.size = new_gap_size;
        _mem_add_to_gap_ix(pool_mgr, new_gap_size, new_gap_node);

        //insert gap node into list
        insert_node_heap(alloc_node, new_gap_node);
    }

    // Update pool variables
    pool_mgr->used_nodes++;
    pool->alloc_size += req_size;
    pool->num_allocs++;
    if (new_gap_size == 0){
        pool->num_gaps--;
    }
    // adjust node heap:
    //   if remaining gap, need a new node
    //   find an unused one in the node heap
    //   make sure one was found
    //   initialize it to a gap node
    //   update metadata (used_nodes)
    //   update linked list (new node right after the node for allocation)
    //   add to gap index
    //   check if successful
    // return allocation record by casting the node to (alloc_pt)

    return (alloc_pt)alloc_node;
}

alloc_status mem_del_alloc(pool_pt pool, alloc_pt alloc) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    // get node from alloc by casting the pointer to (node_pt)
    // find the node in the node heap
    // this is node-to-delete
    // make sure it's found
    // convert to gap node
    // update metadata (num_allocs, alloc_size)
    // if the next node in the list is also a gap, merge into node-to-delete
    //   remove the next node from gap index
    //   check success
    //   add the size to the node-to-delete
    //   update node as unused
    //   update metadata (used nodes)
    //   update linked list:
    /*
                    if (next->next) {
                        next->next->prev = node_to_del;
                        node_to_del->next = next->next;
                    } else {
                        node_to_del->next = NULL;
                    }
                    next->next = NULL;
                    next->prev = NULL;
     */

    // this merged node-to-delete might need to be added to the gap index
    // but one more thing to check...
    // if the previous node in the list is also a gap, merge into previous!
    //   remove the previous node from gap index
    //   check success
    //   add the size of node-to-delete to the previous
    //   update node-to-delete as unused
    //   update metadata (used_nodes)
    //   update linked list
    /*
                    if (node_to_del->next) {
                        prev->next = node_to_del->next;
                        node_to_del->next->prev = prev;
                    } else {
                        prev->next = NULL;
                    }
                    node_to_del->next = NULL;
                    node_to_del->prev = NULL;
     */
    //   change the node to add to the previous node!
    // add the resulting node to the gap index
    // check success

    return ALLOC_FAIL;
}

void mem_inspect_pool(pool_pt pool,
                      pool_segment_pt *segments,
                      unsigned *num_segments) {
    // get the mgr from the pool
    // allocate the segments array with size == used_nodes
    // check successful
    // loop through the node heap and the segments array
    //    for each node, write the size and allocated in the segment
    // "return" the values:
    /*
                    *segments = segs;
                    *num_segments = pool_mgr->used_nodes;
     */
    pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;

}



/***********************************/
/*                                 */
/* Definitions of static functions */
/*                                 */
/***********************************/
// Checks if pool size is within the capacity fill factor. If pool is too large its size
// is expanded by the mem expand factor.
static alloc_status _mem_resize_pool_store() {
    // check if necessary
    if (((float) pool_store_size / pool_store_capacity)
        > MEM_POOL_STORE_FILL_FACTOR) {
        pool_store = (pool_mgr_pt*) realloc(pool_store, (sizeof(pool_mgr_pt)*pool_store_capacity
                                                         * MEM_EXPAND_FACTOR));
        pool_store_capacity = pool_store_capacity*MEM_EXPAND_FACTOR;
    }
    // don't forget to update capacity variables
    return ALLOC_OK;
}

static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr) {
    if (((float)pool_mgr->used_nodes / pool_mgr->total_nodes) > MEM_NODE_HEAP_FILL_FACTOR){
        node_pt new_node_heap = (node_pt) realloc(pool_mgr->node_heap,
                                                (sizeof(node_t) * pool_mgr->total_nodes*MEM_NODE_HEAP_EXPAND_FACTOR));
        if (new_node_heap != NULL) {
            pool_mgr->total_nodes = pool_mgr->total_nodes * MEM_NODE_HEAP_EXPAND_FACTOR;
            pool_mgr->node_heap = new_node_heap;
            return ALLOC_OK;
        }
        else
            return ALLOC_FAIL;
    }
    return ALLOC_OK;
}

// checks to see if each entry of the gap index array is used,
// if full returns alloc_fail otherwise returns alloc_ok
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr) {
    gap_pt gap_array = pool_mgr->gap_ix;
    int array_full = 0;

    for (int i = 0; i < pool_mgr->gap_ix_capacity; i++){
        if (gap_array[i].node == NULL){
            array_full = 1;
            break;
        }
    }
    if (array_full == 1)
        return ALLOC_FAIL;
    else
        return ALLOC_OK;
}

static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                                       size_t size,
                                       node_pt node) {

    // expand the gap index, if necessary (call the function)
    // add the entry at the end
    // update metadata (num_gaps)
    // sort the gap index (call the function)
    // check success
//    { gap_pt gap_array = (gap_pt) realloc(gap_array, (sizeof(gap_t) * pool_mgr->gap_ix_capacity
//                                                    * MEM_GAP_IX_EXPAND_FACTOR));
//    pool_mgr->gap_ix = gap_array;
//}
    return ALLOC_FAIL;
}


// find gap index node
//    gap_pt gap_index = NULL;
//    gap_pt gap_array = pool_mgr->gap_ix;
//    for (int i = 0; i < pool_mgr->gap_ix_capacity; i++){
//        if (gap_array[i].node == alloc_node){
//            gap_index = &gap_array[i];
//        }
//    }
//    gap_index->size = 0;
//    gap_index->node = NULL;
//    _mem_sort_gap_ix(pool_mgr);

static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                            size_t size,
                                            node_pt node) {
    // find the position of the node in the gap index
    // loop from there to the end of the array:
    //    pull the entries (i.e. copy over) one position up
    //    this effectively deletes the chosen node
    // update metadata (num_gaps)
    // zero out the element at position num_gaps!

    return ALLOC_FAIL;
}

// note: only called by _mem_add_to_gap_ix, which appends a single entry
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr) {
    // the new entry is at the end, so "bubble it up"
    // loop from num_gaps - 1 until but not including 0:
    //    if the size of the current entry is less than the previous (u - 1)
    //       swap them (by copying) (remember to use a temporary variable)

    return ALLOC_FAIL;
}

static void insert_node_heap(node_pt first_node, node_pt insert_node){
    insert_node->next = first_node->next;
    insert_node->next->prev = insert_node;
    first_node->next = insert_node;
    insert_node->prev = first_node;
}


