#include "list.h"

#include "../debug.h"

/* 이중 연결 리스트에는 두 개의 헤더 요소가 있습니다: "head"
   첫 번째 요소 바로 앞에 있고 "tail"은 마지막 요소 바로 뒤에 있습니다.
   앞쪽 헤더의 'prev' 링크와 뒤쪽 헤더의 'next' 링크는 null입니다.
   다른 두 링크는 리스트의 내부 요소를 통해 서로 가리킵니다.

   비어 있는 리스트는 다음과 같습니다:

   +------+     +------+
   <---| head |<--->| tail |--->
   +------+     +------+

   두 개의 요소가 있는 리스트는 다음과 같습니다:

   +------+     +-------+     +-------+     +------+
   <---| head |<--->|   1   |<--->|   2   |<--->| tail |<--->
   +------+     +-------+     +-------+     +------+

   이러한 배열의 대칭성은 리스트 처리에서 많은 특수한 경우를 제거합니다.
   예를 들어, list_remove()를 살펴보면: 포인터 할당이 두 개만 있고 조건문이 없습니다.
   헤더 요소 없이 코드가 어려워질 수 있습니다.

   (각 헤더 요소 중 하나의 포인터만 사용되기 때문에,
   실제로 이러한 단순성을 포기하지 않고 두 개의 헤더 요소를 하나로 병합할 수 있습니다.
   그러나 두 개의 별도 요소를 사용하면 일부 작업에 대해 약간의 확인을 수행할 수 있으며, 이것은 가치가 있을 수 있습니다.) */
/* Our doubly linked lists have two header elements: the "head"
   just before the first element and the "tail" just after the
   last element.  The `prev' link of the front header is null, as
   is the `next' link of the back header.  Their other two links
   point toward each other via the interior elements of the list.

   An empty list looks like this:

   +------+     +------+
   <---| head |<--->| tail |--->
   +------+     +------+

   A list with two elements in it looks like this:

   +------+     +-------+     +-------+     +------+
   <---| head |<--->|   1   |<--->|   2   |<--->| tail |<--->
   +------+     +-------+     +-------+     +------+

   The symmetry of this arrangement eliminates lots of special
   cases in list processing.  For example, take a look at
   list_remove(): it takes only two pointer assignments and no
   conditionals.  That's a lot simpler than the code would be
   without header elements.

   (Because only one of the pointers in each header element is used,
   we could in fact combine them into a single header element
   without sacrificing this simplicity.  But using two separate
   elements allows us to do a little bit of checking on some
   operations, which can be valuable.) */

static bool is_sorted(struct list_elem *a, struct list_elem *b, list_less_func *less, void *aux) UNUSED;

/* ELEM이 헤더이면 true를 반환하고, 그렇지 않으면 false를 반환합니다. */
/* Returns true if ELEM is a head, false otherwise. */
static inline bool is_head(struct list_elem *elem) {
    return elem != NULL && elem->prev == NULL && elem->next != NULL;
}

/* ELEM이 내부 요소이면 true를 반환하고, 그렇지 않으면 false를 반환합니다. */
/* Returns true if ELEM is an interior element,
   false otherwise. */
static inline bool is_interior(struct list_elem *elem) {
    return elem != NULL && elem->prev != NULL && elem->next != NULL;
}

/* ELEM이 tail이면 true를 반환하고, 그렇지 않으면 false를 반환합니다. */
/* Returns true if ELEM is a tail, false otherwise. */
static inline bool is_tail(struct list_elem *elem) {
    return elem != NULL && elem->prev != NULL && elem->next == NULL;
}

/* LIST를 빈 리스트로 초기화합니다. */
/* Initializes LIST as an empty list. */
void list_init(struct list *list) {
    ASSERT(list != NULL);
    list->head.prev = NULL;
    list->head.next = &list->tail;
    list->tail.prev = &list->head;
    list->tail.next = NULL;
}

/* LIST의 시작을 반환합니다. */
/* Returns the beginning of LIST.  */
struct list_elem *list_begin(struct list *list) {
    ASSERT(list != NULL);
    return list->head.next;
}

/* ELEM의 다음 요소를 반환합니다.
   ELEM이 리스트의 마지막 요소인 경우, 리스트 tail을 반환합니다.
   ELEM이 리스트 tail인 경우 결과는 정의되지 않습니다. */
/* Returns the element after ELEM in its list.  If ELEM is the
   last element in its list, returns the list tail.  Results are
   undefined if ELEM is itself a list tail. */
struct list_elem *list_next(struct list_elem *elem) {
    ASSERT(is_head(elem) || is_interior(elem));
    return elem->next;
}

/* LIST의 tail을 반환합니다.

   list_end()는 종종 리스트를 앞에서 뒤로 반복하는 데 사용됩니다.
   예제는 list.h 맨 위에 있는 큰 주석을 참조하세요. */
/* Returns LIST's tail.

   list_end() is often used in iterating through a list from
   front to back.  See the big comment at the top of list.h for
   an example. */
struct list_elem *list_end(struct list *list) {
    ASSERT(list != NULL);
    return &list->tail;
}

/* LIST를 역순으로 반복하는 데 사용되는 LIST의 역순 시작을 반환합니다. */
/* Returns the LIST's reverse beginning, for iterating through
   LIST in reverse order, from back to front. */
struct list_elem *list_rbegin(struct list *list) {
    ASSERT(list != NULL);
    return list->tail.prev;
}

/* ELEM의 이전 요소를 반환합니다.
   ELEM이 리스트의 첫 번째 요소인 경우, 리스트 head를 반환합니다.
   ELEM이 리스트 head인 경우 결과는 정의되지 않습니다. */
/* Returns the element before ELEM in its list.  If ELEM is the
   first element in its list, returns the list head.  Results are
   undefined if ELEM is itself a list head. */
struct list_elem *list_prev(struct list_elem *elem) {
    ASSERT(is_interior(elem) || is_tail(elem));
    return elem->prev;
}

/* LIST의 head를 반환합니다.

   list_rend()는 종종 리스트를 역순으로 반복하는 데 사용됩니다.
   아래는 list.h 맨 위에서의 예제를 따르는 일반적인 사용법입니다:

   for (e = list_rbegin(&foo_list); e != list_rend(&foo_list);
   e = list_prev(e))
   {
   struct foo *f = list_entry(e, struct foo, elem);
   ...f를 사용하여 작업...
   } */
/* Returns LIST's head.

   list_rend() is often used in iterating through a list in
   reverse order, from back to front.  Here's typical usage,
   following the example from the top of list.h:

   for (e = list_rbegin (&foo_list); e != list_rend (&foo_list);
   e = list_prev (e))
   {
   struct foo *f = list_entry (e, struct foo, elem);
   ...do something with f...
   }
   */
struct list_elem *list_rend(struct list *list) {
    ASSERT(list != NULL);
    return &list->head;
}

/* LIST의 head를 반환합니다.

   list_head()는 리스트를 반복하는 대체 스타일로 사용할 수 있습니다. 예:

   e = list_head(&list);
   while ((e = list_next(e)) != list_end(&list))
   {
   ...
   } */
/* Return's LIST's head.

   list_head() can be used for an alternate style of iterating
   through a list, e.g.:

   e = list_head (&list);
   while ((e = list_next (e)) != list_end (&list))
   {
   ...
   }
   */
struct list_elem *list_head(struct list *list) {
    ASSERT(list != NULL);
    return &list->head;
}

/* LIST의 tail을 반환합니다. */
/* Return's LIST's tail. */
struct list_elem *list_tail(struct list *list) {
    ASSERT(list != NULL);
    return &list->tail;
}

/* BEFORE의 바로 앞에 ELEM을 삽입합니다.
   BEFORE는 내부 요소이거나 tail일 수 있습니다.
   후자의 경우 list_push_back()과 동일합니다. */
/* Inserts ELEM just before BEFORE, which may be either an
   interior element or a tail.  The latter case is equivalent to
   list_push_back(). */
void list_insert(struct list_elem *before, struct list_elem *elem) {
    ASSERT(is_interior(before) || is_tail(before));
    ASSERT(elem != NULL);

    elem->prev = before->prev;
    elem->next = before;
    before->prev->next = elem;
    before->prev = elem;
}

/* FIRST부터 LAST(불포함)까지의 요소를 현재 리스트에서 제거한 후,
   BEFORE의 바로 앞에 삽입합니다. BEFORE는 내부 요소이거나 tail일 수 있습니다. */
/* Removes elements FIRST though LAST (exclusive) from their
   current list, then inserts them just before BEFORE, which may
   be either an interior element or a tail. */
void list_splice(struct list_elem *before, struct list_elem *first, struct list_elem *last) {
    ASSERT(is_interior(before) || is_tail(before));
    if (first == last)
        return;
    last = list_prev(last);

    ASSERT(is_interior(first));
    ASSERT(is_interior(last));

    /* 현재 리스트에서 FIRST부터 LAST까지를 정리하여 제거합니다. */
    /* Cleanly remove FIRST...LAST from its current list. */
    first->prev->next = last->next;
    last->next->prev = first->prev;

    /* FIRST부터 LAST까지를 새로운 리스트에 삽입합니다. */
    /* Splice FIRST...LAST into new list. */
    first->prev = before->prev;
    last->next = before;
    before->prev->next = first;
    before->prev = last;
}

/* ELEM을 LIST의 시작 부분에 삽입하여 LIST에서 front가 됩니다. */
/* Inserts ELEM at the beginning of LIST, so that it becomes the
   front in LIST. */
void list_push_front(struct list *list, struct list_elem *elem) {
    list_insert(list_begin(list), elem);
}

/* ELEM을 LIST의 끝에 삽입하여 LIST에서 back이 됩니다. */
/* Inserts ELEM at the end of LIST, so that it becomes the
   back in LIST. */
void list_push_back(struct list *list, struct list_elem *elem) {
    list_insert(list_end(list), elem);
}

/* ELEM을 해당 리스트에서 제거하고 그 뒤를 따르는 요소를 반환합니다.
   ELEM이 리스트에 없는 경우 정의되지 않은 동작입니다.

   ELEM을 제거한 후에는 ELEM을 리스트의 요소로 취급하는 것은 안전하지 않습니다.
   특히, 제거 후에 list_next() 또는 list_prev()를 사용하는 것은 정의되지 않은 동작을 유발합니다.
   이는 리스트에서 요소를 제거하는 간단한 루프가 실패할 수 있음을 의미합니다:

 ** 이렇게 하지 마세요 **
 for (e = list_begin (&list); e != list_end (&list); e = list_next (e))
 {
 ... e를 사용하는 작업 ...
 list_remove (e);
 }
 ** 이렇게 하지 마세요 **

 리스트에서 요소를 반복하고 제거하는 올바른 방법은 다음과 같습니다:

for (e = list_begin (&list); e != list_end (&list); e = list_remove (e))
{
... e를 사용하는 작업 ...
}

리스트의 요소를 free()해야하는 경우 더 조심스러워야합니다.
다음은 심지어 그 경우에도 작동하는 대안적인 전략입니다:

while (!list_empty (&list))
{
struct list_elem *e = list_pop_front (&list);
... e를 사용하는 작업 ...
}
*/
/* Removes ELEM from its list and returns the element that
   followed it.  Undefined behavior if ELEM is not in a list.

   It's not safe to treat ELEM as an element in a list after
   removing it.  In particular, using list_next() or list_prev()
   on ELEM after removal yields undefined behavior.  This means
   that a naive loop to remove the elements in a list will fail:

 ** DON'T DO THIS **
 for (e = list_begin (&list); e != list_end (&list); e = list_next (e))
 {
 ...do something with e...
 list_remove (e);
 }
 ** DON'T DO THIS **

 Here is one correct way to iterate and remove elements from a
list:

for (e = list_begin (&list); e != list_end (&list); e = list_remove (e))
{
...do something with e...
}

If you need to free() elements of the list then you need to be
more conservative.  Here's an alternate strategy that works
even in that case:

while (!list_empty (&list))
{
struct list_elem *e = list_pop_front (&list);
...do something with e...
}
*/
struct list_elem *list_remove(struct list_elem *elem) {
    ASSERT(is_interior(elem));
    elem->prev->next = elem->next;
    elem->next->prev = elem->prev;
    return elem->next;
}

/* 리스트의 맨 앞 요소를 제거하고 반환합니다.
   제거 전 LIST가 비어있는 경우 정의되지 않은 동작입니다. */
/* Removes the front element from LIST and returns it.
   Undefined behavior if LIST is empty before removal. */
struct list_elem *list_pop_front(struct list *list) {
    struct list_elem *front = list_front(list);
    list_remove(front);
    return front;
}

/* 리스트의 맨 뒤 요소를 제거하고 반환합니다.
   제거 전 LIST가 비어있는 경우 정의되지 않은 동작입니다. */
/* Removes the back element from LIST and returns it.
   Undefined behavior if LIST is empty before removal. */
struct list_elem *list_pop_back(struct list *list) {
    struct list_elem *back = list_back(list);
    list_remove(back);
    return back;
}

/* 리스트의 맨 앞 요소를 반환합니다.
   LIST가 비어있는 경우 정의되지 않은 동작입니다. */
/* Returns the front element in LIST.
   Undefined behavior if LIST is empty. */
struct list_elem *list_front(struct list *list) {
    ASSERT(!list_empty(list));
    return list->head.next;
}

/* 리스트의 맨 뒤 요소를 반환합니다.
   LIST가 비어있는 경우 정의되지 않은 동작입니다. */
/* Returns the back element in LIST.
   Undefined behavior if LIST is empty. */
struct list_elem *list_back(struct list *list) {
    ASSERT(!list_empty(list));
    return list->tail.prev;
}

/* 리스트의 요소 수를 반환합니다.
   요소 수에 따라 O(n) 시간이 걸립니다. */
/* Returns the number of elements in LIST.
   Runs in O(n) in the number of elements. */
size_t list_size(struct list *list) {
    struct list_elem *e;
    size_t cnt = 0;

    for (e = list_begin(list); e != list_end(list); e = list_next(e))
        cnt++;
    return cnt;
}

/* 리스트가 비어있으면 true를 반환하고, 그렇지 않으면 false를 반환합니다. */
/* Returns true if LIST is empty, false otherwise. */
bool list_empty(struct list *list) {
    return list_begin(list) == list_end(list);
}

/* A와 B가 가리키는 'struct list_elem *'를 교환합니다. */
/* Swaps the `struct list_elem *'s that A and B point to. */
static void swap(struct list_elem **a, struct list_elem **b) {
    struct list_elem *t = *a;
    *a = *b;
    *b = t;
}

/* LIST의 순서를 반전시킵니다. */
/* Reverses the order of LIST. */
void list_reverse(struct list *list) {
    if (!list_empty(list)) {
        struct list_elem *e;

        for (e = list_begin(list); e != list_end(list); e = e->prev)
            swap(&e->prev, &e->next);
        swap(&list->head.next, &list->tail.prev);
        swap(&list->head.next->prev, &list->tail.prev->next);
    }
}

/* AUX를 고려하여 리스트 요소 A부터 B(배타적)가 순서대로 있는 경우에만 true를 반환합니다. */
/* Returns true only if the list elements A through B (exclusive)
   are in order according to LESS given auxiliary data AUX. */
static bool is_sorted(struct list_elem *a, struct list_elem *b, list_less_func *less, void *aux) {
    if (a != b)
        while ((a = list_next(a)) != b)
            if (less(a, list_prev(a), aux))
                return false;
    return true;
}

/* 리스트 요소 A에서 시작하여 AUX를 고려하여 NONDECREASING 순서로 정렬된 run을 찾습니다.
   B 이후에 끝나지 않습니다. 런의 (배타적) 끝을 반환합니다.
   A부터 B(배타적)까지는 비어있지 않은 범위를 형성해야 합니다. */
/* Finds a run, starting at A and ending not after B, of list
   elements that are in nondecreasing order according to LESS
   given auxiliary data AUX.  Returns the (exclusive) end of the
   run.
   A through B (exclusive) must form a non-empty range. */
static struct list_elem *find_end_of_run(struct list_elem *a, struct list_elem *b, list_less_func *less, void *aux) {
    ASSERT(a != NULL);
    ASSERT(b != NULL);
    ASSERT(less != NULL);
    ASSERT(a != b);

    do {
        a = list_next(a);
    } while (a != b && !less(a, list_prev(a), aux));
    return a;
}

/* A0부터 A1B0(배타적)과 A1B0부터 B1(배타적)을 결합하여 
   B1(배타적)로 끝나는 결합된 범위를 형성합니다.
   두 입력 범위는 비어있지 않고, AUX를 고려하여 NONDECREASING 순서로 정렬되어야 합니다. 
   출력 범위도 동일한 방식으로 정렬됩니다. */
/* Merges A0 through A1B0 (exclusive) with A1B0 through B1
   (exclusive) to form a combined range also ending at B1
   (exclusive).  Both input ranges must be nonempty and sorted in
   nondecreasing order according to LESS given auxiliary data
   AUX.  The output range will be sorted the same way. */
static void inplace_merge(struct list_elem *a0, struct list_elem *a1b0, struct list_elem *b1, list_less_func *less, void *aux) {
    ASSERT(a0 != NULL);
    ASSERT(a1b0 != NULL);
    ASSERT(b1 != NULL);
    ASSERT(less != NULL);
    ASSERT(is_sorted(a0, a1b0, less, aux));
    ASSERT(is_sorted(a1b0, b1, less, aux));

    while (a0 != a1b0 && a1b0 != b1)
        if (!less(a1b0, a0, aux))
            a0 = list_next(a0);
        else {
            a1b0 = list_next(a1b0);
            list_splice(a0, list_prev(a1b0), a1b0);
        }
}

/* AUX를 고려하여 LIST를 정렬합니다. LIST의 요소 수에 따라 O(n lg n) 시간과 O(1) 공간을 사용하는
   자연스러운 반복적인 병합 정렬을 사용합니다. */
/* Sorts LIST according to LESS given auxiliary data AUX, using a
   natural iterative merge sort that runs in O(n lg n) time and
   O(1) space in the number of elements in LIST. */
void list_sort(struct list *list, list_less_func *less, void *aux) {
    size_t output_run_cnt; /* Number of runs output in current pass. */

    ASSERT(list != NULL);
    ASSERT(less != NULL);

    /* 하나의 run만 남을 때까지 인접한 nondecreasing 요소의 run을
       반복적으로 병합합니다. */
    /* Pass over the list repeatedly, merging adjacent runs of
       nondecreasing elements, until only one run is left. */
    do {
        struct list_elem *a0;   /* 첫 번째 run의 시작점. *//* Start of first run. */
        struct list_elem *a1b0; /* 첫 번째 run의 끝, 두 번째 run의 시작점. *//* End of first run, start of second. */
        struct list_elem *b1;   /* 두 번째 run의 끝. *//* End of second run. */

        output_run_cnt = 0;
        for (a0 = list_begin(list); a0 != list_end(list); a0 = b1) {
            /* 각 반복은 하나의 출력 run을 생성합니다. */
            /* Each iteration produces one output run. */
            output_run_cnt++;

            /* nondecreasing 요소의 두 인접 run인 A0...A1B0와 A1B0...B1을 찾습니다. */
            /* Locate two adjacent runs of nondecreasing elements
               A0...A1B0 and A1B0...B1. */
            a1b0 = find_end_of_run(a0, list_end(list), less, aux);
            if (a1b0 == list_end(list))
                break;
            b1 = find_end_of_run(a1b0, list_end(list), less, aux);

            /* run을 병합합니다. */
            /* Merge the runs. */
            inplace_merge(a0, a1b0, b1, less, aux);
        }
    } while (output_run_cnt > 1);

    ASSERT(is_sorted(list_begin(list), list_end(list), less, aux));
}

/* AUX를 고려하여 정렬된 LIST의 적절한 위치에 ELEM을 삽입합니다.
   LIST의 요소 수에 따라 O(n) 평균 케이스 시간이 소요됩니다. */
/* Inserts ELEM in the proper position in LIST, which must be
   sorted according to LESS given auxiliary data AUX.
   Runs in O(n) average case in the number of elements in LIST. */
void list_insert_ordered(struct list *list, struct list_elem *elem, list_less_func *less, void *aux) {
    struct list_elem *e;

    ASSERT(list != NULL);
    ASSERT(elem != NULL);
    ASSERT(less != NULL);

    for (e = list_begin(list); e != list_end(list); e = list_next(e))
        if (less(elem, e, aux))
            break;
    return list_insert(e, elem);
}

/* 내림 차순 삽입 */
void list_insert_desc_ordered(struct list *list, struct list_elem *elem, list_less_func *less, void *aux) {
    struct list_elem *e;

    ASSERT(list != NULL);
    ASSERT(elem != NULL);
    ASSERT(less != NULL);

    for (e = list_begin(list); e != list_end(list); e = list_next(e))
        if (less(e, elem, aux)) {
            break;
        }
    return list_insert(e, elem);
}

/* LIST를 반복하면서 LESS 및 보조 데이터 AUX에 따라 동등한 인접 요소 집합에서 첫 번째를 제외한 모든 요소를 제거합니다.
   DUPLICATES가 널이 아닌 경우 LIST의 요소는 DUPLICATES에 추가됩니다. */
/* Iterates through LIST and removes all but the first in each
   set of adjacent elements that are equal according to LESS
   given auxiliary data AUX.  If DUPLICATES is non-null, then the
   elements from LIST are appended to DUPLICATES. */
void list_unique(struct list *list, struct list *duplicates, list_less_func *less, void *aux) {
    struct list_elem *elem, *next;

    ASSERT(list != NULL);
    ASSERT(less != NULL);
    if (list_empty(list))
        return;

    elem = list_begin(list);
    while ((next = list_next(elem)) != list_end(list))
        if (!less(elem, next, aux) && !less(next, elem, aux)) {
            list_remove(next);
            if (duplicates != NULL)
                list_push_back(duplicates, next);
        } else
            elem = next;
}

/* LIST에서 가장 큰 값을 가진 요소를 반환합니다.
   LESS 및 보조 데이터 AUX에 따라 여러 최대 값이 있는 경우 목록에서 먼저 나타나는 값을 반환합니다.
   리스트가 비어있는 경우 꼬리를 반환합니다. */
/* Returns the element in LIST with the largest value according
   to LESS given auxiliary data AUX.  If there is more than one
   maximum, returns the one that appears earlier in the list.  If
   the list is empty, returns its tail. */
struct list_elem *list_max(struct list *list, list_less_func *less, void *aux) {
    struct list_elem *max = list_begin(list);
    if (max != list_end(list)) {
        struct list_elem *e;

        for (e = list_next(max); e != list_end(list); e = list_next(e))
            if (less(max, e, aux))
                max = e;
    }
    return max;
}

/* LIST에서 가장 작은 값을 가진 요소를 반환합니다.
   LESS 및 보조 데이터 AUX에 따라 여러 최소 값이 있는 경우 목록에서 먼저 나타나는 값을 반환합니다.
   리스트가 비어있는 경우 꼬리를 반환합니다. */
/* Returns the element in LIST with the smallest value according
   to LESS given auxiliary data AUX.  If there is more than one
   minimum, returns the one that appears earlier in the list.  If
   the list is empty, returns its tail. */
struct list_elem *list_min(struct list *list, list_less_func *less, void *aux) {
    struct list_elem *min = list_begin(list);
    if (min != list_end(list)) {
        struct list_elem *e;

        for (e = list_next(min); e != list_end(list); e = list_next(e))
            if (less(e, min, aux))
                min = e;
    }
    return min;
}
