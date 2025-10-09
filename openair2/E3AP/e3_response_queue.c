/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "e3_response_queue.h"
#include <stdlib.h>
#include <errno.h>
#include <time.h>

e3_response_queue_t* e3_response_queue_create(void) {
  e3_response_queue_t *queue = malloc(sizeof(e3_response_queue_t));
  if (!queue) return NULL;
  
  queue->head = 0;
  queue->tail = 0;
  queue->count = 0;
  pthread_mutex_init(&queue->mutex, NULL);
  pthread_cond_init(&queue->cond, NULL);
  return queue;
}

void e3_response_queue_destroy(e3_response_queue_t *queue) {
  if (queue) {
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
    free(queue);
  }
}

int e3_response_queue_push(e3_response_queue_t *queue, const e3ap_pdu_t *pdu) {
  if (!queue || !pdu) return -1;
  
  pthread_mutex_lock(&queue->mutex);
  if (queue->count >= RESPONSE_QUEUE_SIZE) {
    pthread_mutex_unlock(&queue->mutex);
    return -1; // Queue full
  }
  
  e3ap_pdu_copy(&queue->messages[queue->tail], pdu);
  queue->tail = (queue->tail + 1) % RESPONSE_QUEUE_SIZE;
  queue->count++;
  
  pthread_cond_signal(&queue->cond);
  pthread_mutex_unlock(&queue->mutex);
  return 0;
}

int e3_response_queue_pop(e3_response_queue_t *queue, e3ap_pdu_t *pdu, int timeout_ms) {
  if (!queue || !pdu) return -1;
  
  pthread_mutex_lock(&queue->mutex);
  
  if (timeout_ms > 0) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
      ts.tv_sec++;
      ts.tv_nsec -= 1000000000;
    }
    
    while (queue->count == 0) {
      int ret = pthread_cond_timedwait(&queue->cond, &queue->mutex, &ts);
      if (ret == ETIMEDOUT) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
      }
    }
  } else {
    while (queue->count == 0) {
      pthread_cond_wait(&queue->cond, &queue->mutex);
    }
  }
  
  e3ap_pdu_copy(pdu, &queue->messages[queue->head]);
  queue->head = (queue->head + 1) % RESPONSE_QUEUE_SIZE;
  queue->count--;
  
  pthread_mutex_unlock(&queue->mutex);
  return 0;
}