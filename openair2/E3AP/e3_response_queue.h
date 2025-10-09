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

#ifndef E3_RESPONSE_QUEUE_H
#define E3_RESPONSE_QUEUE_H

#include <stdint.h>
#include <pthread.h>
#include "e3ap_types.h"

#define RESPONSE_QUEUE_SIZE 100

typedef struct {
  e3ap_pdu_t messages[RESPONSE_QUEUE_SIZE];
  int head, tail, count;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} e3_response_queue_t;

/**
 * Create a new response queue
 * @return Pointer to the created queue, or NULL on failure
 */
e3_response_queue_t* e3_response_queue_create(void);

/**
 * Destroy a response queue and free its resources
 * @param queue Queue to destroy
 */
void e3_response_queue_destroy(e3_response_queue_t *queue);

/**
 * Push a message to the response queue
 * @param queue Target queue
 * @param pdu E3AP PDU to push
 * @return 0 on success, -1 on failure (queue full)
 */
int e3_response_queue_push(e3_response_queue_t *queue, const e3ap_pdu_t *pdu);

/**
 * Pop a message from the response queue
 * @param queue Source queue
 * @param pdu Buffer to store the popped E3AP PDU
 * @param timeout_ms Timeout in milliseconds (0 = wait indefinitely)
 * @return 0 on success, -1 on timeout or failure
 */
int e3_response_queue_pop(e3_response_queue_t *queue, e3ap_pdu_t *pdu, int timeout_ms);

#endif /* E3_RESPONSE_QUEUE_H */