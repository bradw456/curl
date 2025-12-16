/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at https://curl.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 * SPDX-License-Identifier: curl
 *
 ***************************************************************************/
#include "first.h"

#ifndef CURL_DISABLE_WEBSOCKETS

static int t2724_run_multi_loop(CURLM *multi)
{
  int still_running = 0;
  CURLMcode mc;

  do {
    mc = curl_multi_perform(multi, &still_running);
    if (mc != CURLM_OK) {
      fprintf(stderr, "curl_multi_perform failed: %s\n", curl_multi_strerror(mc));
      return 1;
    }

    if (still_running) {
        mc = curl_multi_wait(multi, NULL, 0, TEST_HANG_TIMEOUT, NULL);
        if (mc != CURLM_OK) {
            fprintf(stderr, "curl_multi_wait failed: %s\n", curl_multi_strerror(mc));
            return 1;
        }
    }
  } while (still_running);

  return 0;
}
#endif /* CURL_DISABLE_WEBSOCKETS */

static CURLcode test_lib2724(const char *URL)
{
#ifndef CURL_DISABLE_WEBSOCKETS
  /* Create a WebSocket request using an easy handle that is added to a multi handle.  Send a
   * request that isn't upgraded.  Verify that with CURLWS_UPGRD_REFUSED_OK the connection is kept
   * open and there isn't an error.  Send another request with the same multi handle and therefore
   * the same connection pool.  Verify that the second request succeeds and reuses the previous
   * connection. */
  CURL *easy_ws_refused = NULL;
  CURL *easy_http_ok = NULL;
  CURLM *multi = NULL;
  int res = 0;
  long response_code = 0;
  CURLMsg *msg;
  int msgs_in_queue;
  char target_url[256];
  const char *port = libtest_arg3;
  const char *address = libtest_arg2;
  (void)URL;

  curl_global_init(CURL_GLOBAL_ALL);

  multi_init(multi);

  /* 1. Setup WebSocket upgrade refused (expect non-101) */

  easy_init(easy_ws_refused);

  curl_msnprintf(target_url, sizeof(target_url), "ws://%s:%s/path/ws/2724",
                 address, port);
  target_url[sizeof(target_url) - 1] = '\0';
  easy_setopt(easy_ws_refused, CURLOPT_URL, target_url);
  easy_setopt(easy_ws_refused, CURLOPT_VERBOSE, 1L);
  /* This is being tested. Prevents a refused upgrade from being treated as an error. */
  easy_setopt(easy_ws_refused, CURLOPT_WS_OPTIONS, (long)CURLWS_UPGRD_REFUSED_OK);

  multi_add_handle(multi, easy_ws_refused);

  if (t2724_run_multi_loop(multi)) {
    res = 1;
    goto test_cleanup;
  }

  msg = curl_multi_info_read(multi, &msgs_in_queue);
  if (msg && msg->easy_handle == easy_ws_refused && msg->msg == CURLMSG_DONE) {
    curl_easy_getinfo(easy_ws_refused, CURLINFO_RESPONSE_CODE, &response_code);

    fprintf(stderr, "Request 1 (WS Fail) completed. HTTP Code: %ld.\n", response_code);

    if (response_code == 101) {
      fprintf(stderr, "TEST FAILURE: Request 1 unexpectedly returned 101 (WebSocket Upgrade).\n");
      res = 1;
    } else {
      fprintf(stderr, "TEST SUCCESS: Request 1 returned non-101 (%ld), as expected.\n", response_code);
    }
  } else {
    fprintf(stderr, "TEST FAILURE: Request 1 did not complete or multi_info_read failed.\n");
    res = 1;
  }

  multi_remove_handle(multi, easy_ws_refused);
  curl_easy_cleanup(easy_ws_refused);
  easy_ws_refused = NULL;

  /* 2. Follow up with an http request. Expect to resuse the connection. */

  easy_init(easy_http_ok);

  /* Set URL to a standard HTTP target, expected to succeed with 200 */
  curl_msnprintf(target_url, sizeof(target_url), "http://%s:%s/path/http/2724",
                 address, port);
  target_url[sizeof(target_url) - 1] = '\0';
  easy_setopt(easy_http_ok, CURLOPT_URL, target_url);
  easy_setopt(easy_http_ok, CURLOPT_VERBOSE, 1L);

  multi_add_handle(multi, easy_http_ok);

  /* Perform the second request using the same multi handle */
  if (t2724_run_multi_loop(multi)) {
    res = 1;
    goto test_cleanup;
  }

  msg = curl_multi_info_read(multi, &msgs_in_queue);
  if (msg && msg->easy_handle == easy_http_ok && msg->msg == CURLMSG_DONE) {
    if (msg->data.result != CURLE_OK) {
        fprintf(stderr, "TEST FAILURE: Request 2 transfer failed: %s\n", curl_easy_strerror(msg->data.result));
        res = 1;
    }

    curl_easy_getinfo(easy_http_ok, CURLINFO_RESPONSE_CODE, &response_code);

    fprintf(stderr, "Request 2 (HTTP OK) completed. HTTP Code: %ld.\n", response_code);

    if (response_code != 200) {
      fprintf(stderr, "TEST FAILURE: Request 2 returned %ld, expected 200.\n", response_code);
      res = 1;
    } else {
      fprintf(stderr, "TEST SUCCESS: Request 2 returned 200 response code, as expected.\n");
    }
  } else {
    fprintf(stderr, "TEST FAILURE: Request 2 did not complete successfully.\n");
    res = 1;
  }

test_cleanup:
  if (easy_http_ok) curl_multi_remove_handle(multi, easy_http_ok);
  if (easy_http_ok) curl_easy_cleanup(easy_http_ok);
  if (multi) curl_multi_cleanup(multi);
  curl_global_cleanup();

  return res;
#else
  NO_SUPPORT_BUILT_IN
#endif
}
