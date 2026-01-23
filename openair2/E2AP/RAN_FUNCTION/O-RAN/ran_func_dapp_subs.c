#include "ran_func_dapp_subs.h"
#include "common/utils/assertions.h"
#include "common/utils/alg/find.h"

#include <assert.h>
#include <pthread.h>

static pthread_mutex_t dapp_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_map_mtx = PTHREAD_MUTEX_INITIALIZER;
static seq_arr_t g_all_fmt0_subs;
static bool g_map_inited = false;

/**
 * @brief Predicate to compare a RIC request ID with a stored uint32_t entry.
 */
static bool eq_int_fmt0(const void *value, const void *it)
{
  const uint32_t ric_req_id = *(const uint32_t *)value;
  const uint32_t *stored = (const uint32_t *)it;
  return ric_req_id == *stored;
}

/**
 * @brief Initialize per-SM DAPP subscription data (event triggers + fmt0 list).
 */
void init_dapp_subs_data(dapp_subs_data_t *dapp_subs_data)
{
  pthread_mutex_lock(&dapp_mutex);
  seq_arr_init(&dapp_subs_data->fmt_0_subs, sizeof(uint32_t));
  pthread_mutex_unlock(&dapp_mutex);
}

/**
 * @brief Remove all per-SM subscription state for a given RIC request ID.
 *
 * This function:
 *  - Searches for @p ric_req_id in the per-SM fmt_0_subs list.
 *  - If found, erases the entry from the local list and then calls
 *    ric_subs_remove(ric_req_id) to drop it from the global registry.
 *  - If not found, logs a warning that an unknown RIC request ID was removed.
 */
void remove_dapp_subs_data(dapp_subs_data_t *dapp_subs_data, uint32_t ric_req_id)
{
  pthread_mutex_lock(&dapp_mutex);

  // Look for ric_req_id only in the fmt_0_subs list
  elm_arr_t elm_fmt0 = find_if(&dapp_subs_data->fmt_0_subs, &ric_req_id, eq_int_fmt0);
  const bool had_fmt0 = (elm_fmt0.it != NULL);

  if (had_fmt0) {
    seq_arr_erase(&dapp_subs_data->fmt_0_subs, elm_fmt0.it);
  } else {
    printf("[E2 AGENT][WARN] Tried to remove unknown RIC request ID: %u\n", ric_req_id);
  }

  pthread_mutex_unlock(&dapp_mutex);

  if (had_fmt0) {
    ric_subs_remove(ric_req_id);
  }
}

/**
 * @brief One-time initialization of the global list of fmt0 subscriptions.
 */
static void subs_init_once(void)
{
  if (!g_map_inited) {
    seq_arr_init(&g_all_fmt0_subs, sizeof(uint32_t));
    g_map_inited = true;
  }
}

/**
 * @brief Check if a sequence of uint32_t contains a given RIC request ID.
 */
static bool seq_contains_req_id(const seq_arr_t *s, uint32_t v)
{
  for (void *it = seq_arr_front((seq_arr_t *)s); it != seq_arr_end((seq_arr_t *)s); it = seq_arr_next(s, it))
    if (*(uint32_t *)it == v)
      return true;
  return false;
}

/**
 * @brief Append a RIC request ID to a sequence if it is not already present.
 */
static void seq_push_unique_req_id(seq_arr_t *s, uint32_t v)
{
  if (!seq_contains_req_id(s, v))
    seq_arr_push_back(s, &v, sizeof(v));
}

/**
 * @brief Remove the first occurrence of a RIC request ID from a sequence.
 *
 * @return true if an element was removed, false otherwise.
 */
static bool seq_remove_first_req_id(seq_arr_t *s, uint32_t v)
{
  for (void *it = seq_arr_front(s); it != seq_arr_end(s); it = seq_arr_next(s, it)) {
    if (*(uint32_t *)it == v) {
      seq_arr_erase(s, it);
      return true;
    }
  }
  return false;
}

/**
 * @brief Track a new format-0 subscription ID in the per-SM data structure.
 */
void insert_fmt_0_ric_id(dapp_subs_data_t *d, uint32_t ric_req_id)
{
  pthread_mutex_lock(&dapp_mutex);
  seq_arr_push_back(&d->fmt_0_subs, &ric_req_id, sizeof(ric_req_id));
  pthread_mutex_unlock(&dapp_mutex);
}

/**
 * @brief Add a RIC request ID to the global list of format-0 subscriptions.
 */
void ric_subs_add(uint32_t ric_req_id)
{
  pthread_mutex_lock(&g_map_mtx);
  subs_init_once();
  seq_push_unique_req_id(&g_all_fmt0_subs, ric_req_id);
  pthread_mutex_unlock(&g_map_mtx);
}

/**
 * @brief Snapshot all globally registered format-0 RIC subscription request IDs.
 */
size_t ric_subs_snapshot(uint32_t **out)
{
  assert(out != NULL);
  *out = NULL;

  pthread_mutex_lock(&g_map_mtx);
  subs_init_once();

  size_t n = 0;
  for (void *it = seq_arr_front(&g_all_fmt0_subs);
       it != seq_arr_end(&g_all_fmt0_subs);
       it = seq_arr_next(&g_all_fmt0_subs, it)) {
    ++n;
  }

  if (n == 0) {
    pthread_mutex_unlock(&g_map_mtx);
    return 0;
  }

  uint32_t *ids = malloc(n * sizeof(*ids));
  if (ids == NULL) {
    pthread_mutex_unlock(&g_map_mtx);
    return 0;
  }

  size_t i = 0;
  for (void *it = seq_arr_front(&g_all_fmt0_subs);
       it != seq_arr_end(&g_all_fmt0_subs);
       it = seq_arr_next(&g_all_fmt0_subs, it)) {
    ids[i++] = *(uint32_t *)it;
  }

  pthread_mutex_unlock(&g_map_mtx);

  *out = ids;
  return n;
}

/**
 * @brief Remove a RIC request ID from the global list of format-0 subscriptions.
 */
void ric_subs_remove(uint32_t ric_req_id)
{
  pthread_mutex_lock(&g_map_mtx);
  subs_init_once();
  (void)seq_remove_first_req_id(&g_all_fmt0_subs, ric_req_id);
  pthread_mutex_unlock(&g_map_mtx);
}