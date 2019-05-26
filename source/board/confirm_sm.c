/*
 * This file is part of the KeepKey project.
 *
 * Copyright (C) 2015 KeepKey LLC
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

/* === Includes ============================================================ */

#include "board/confirm_sm.h"
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "board/board.h"
#include "board/canvas.h"
#include "board/layout.h"
#include "board/msg_dispatch.h"
#include "driver/button.h"
#include "driver/display.h"
#include "task.h"
#include "trezor-crypto/memzero.h"

#ifndef CONFIDENTIAL
#define CONFIDENTIAL
#endif
/* Button request ack */
static bool m_button_request_acked = false;

extern bool m_reset_msg_stack;

static CONFIDENTIAL char strbuf[BODY_CHAR_MAX];

/// Handler for push button being pressed.
/// \param context current state context.
static void handle_screen_press(void *context) {
    assert(context != NULL);

    StateInfo *si = (StateInfo *)context;

    if (m_button_request_acked) {
        switch (si->display_state) {
            case HOME:
                si->active_layout = LAYOUT_CONFIRM_ANIMATION;
                si->display_state = CONFIRM_WAIT;
                break;

            default:
                break;
        }
    }
}

/// Handler for push button being pressed.
/// \param context current state context.
static void handle_screen_release(void *context) {
    assert(context != NULL);

    StateInfo *si = (StateInfo *)context;

    switch (si->display_state) {
        case CONFIRM_WAIT:
            si->active_layout = LAYOUT_REQUEST_NO_ANIMATION;
            si->display_state = HOME;
            break;

        case CONFIRMED:
            si->active_layout = LAYOUT_FINISHED;
            si->display_state = FINISHED;
            break;

        default:
            break;
    }
}

/// User has held down the push button for duration as requested.
/// \param context current state context.
static void handle_confirm_timeout(void *context) {
    assert(context != NULL);

    StateInfo *si = (StateInfo *)context;
    si->display_state = CONFIRMED;
    si->active_layout = LAYOUT_CONFIRMED;
}

/// Changes the active layout of the confirmation screen.
/// \param active_layout The layout to swtich to.
/// \param si current state information.
/// \param layout_notification_func layout callback for displaying confirm
/// message.
static void swap_layout(ActiveLayout active_layout, volatile StateInfo *si,
                        layout_notification_t layout_notification_func) {
    switch (active_layout) {
        case LAYOUT_REQUEST:
            (*layout_notification_func)(si->lines[active_layout].request_title,
                                        si->lines[active_layout].request_body,
                                        NOTIFICATION_REQUEST);
            break;

        case LAYOUT_REQUEST_NO_ANIMATION:
            (*layout_notification_func)(si->lines[active_layout].request_title,
                                        si->lines[active_layout].request_body,
                                        NOTIFICATION_REQUEST_NO_ANIMATION);
            break;

        case LAYOUT_CONFIRM_ANIMATION:
            (*layout_notification_func)(si->lines[active_layout].request_title,
                                        si->lines[active_layout].request_body,
                                        NOTIFICATION_CONFIRM_ANIMATION);
            break;

        case LAYOUT_CONFIRMED:
            (*layout_notification_func)(si->lines[active_layout].request_title,
                                        si->lines[active_layout].request_body,
                                        NOTIFICATION_CONFIRMED);
            break;

        default:
            assert(0);
    };
}

/// Common confirmation function.
/// \param request_title  The confirmation's title.
/// \param requesta_body  The body of the confirmation message.
/// \param layout_notification_func  layout callback for displaying confirm
/// message. \returns true iff the device confirmed.
static bool confirm_helper(const char *request_title, const char *request_body,
                           layout_notification_t layout_notification_func) {
    bool ret_stat = false;
    volatile StateInfo state_info;
    ActiveLayout new_layout;
    ActiveLayout cur_layout;
    DisplayState new_ds;
    uint16_t tiny_msg;
    static CONFIDENTIAL uint8_t msg_tiny_buf[MSG_TINY_BFR_SZ];

    m_reset_msg_stack = false;

    memset((void *)&state_info, 0, sizeof(state_info));
    state_info.display_state = HOME;
    state_info.active_layout = LAYOUT_REQUEST;

    /* Request */
    state_info.lines[LAYOUT_REQUEST].request_title = request_title;
    state_info.lines[LAYOUT_REQUEST].request_body = request_body;
    state_info.lines[LAYOUT_REQUEST_NO_ANIMATION].request_title = request_title;
    state_info.lines[LAYOUT_REQUEST_NO_ANIMATION].request_body = request_body;

    /* Confirming */
    state_info.lines[LAYOUT_CONFIRM_ANIMATION].request_title = request_title;
    state_info.lines[LAYOUT_CONFIRM_ANIMATION].request_body = request_body;

    /* Confirmed */
    state_info.lines[LAYOUT_CONFIRMED].request_title = request_title;
    state_info.lines[LAYOUT_CONFIRMED].request_body = request_body;

    button_set_confirm_on_press_handler(&handle_screen_press,
                                        (void *)&state_info);
    button_set_confirm_on_release_handler(&handle_screen_release,
                                          (void *)&state_info);
    button_set_confirm_on_longpush_handler(&handle_confirm_timeout,
                                           (void *)&state_info);

    cur_layout = LAYOUT_INVALID;
    uint32_t last_animate_tick;
    while (1) {
        new_layout = state_info.active_layout;
        new_ds = state_info.display_state;
        /* Listen for tiny messages */
        tiny_msg = check_for_tiny_msg(msg_tiny_buf);

        switch (tiny_msg) {
            case MessageType_MessageType_ButtonAck:
                m_button_request_acked = true;
                break;

            case MessageType_MessageType_Cancel:
            case MessageType_MessageType_Initialize:
                if (tiny_msg == MessageType_MessageType_Initialize) {
                    m_reset_msg_stack = true;
                }

                ret_stat = false;
                goto confirm_helper_exit;
            default:
                break; /* break from switch statement and stay in the while
                          loop*/
        }

        if (new_ds == FINISHED) {
            ret_stat = true;
            layout_confirm_finished();
            break; /* confirmation done.  Exiting function */
        }

        if (cur_layout != new_layout) {
            swap_layout(new_layout, &state_info, layout_notification_func);
            cur_layout = new_layout;
        }

        animate();
        last_animate_tick = xTaskGetTickCount();
        layout_commit(false);
        while (xTaskGetTickCount() - last_animate_tick < TIME_TASK_DELAY) {
            vTaskDelay(10);
        }
    }

confirm_helper_exit:
    button_set_confirm_on_press_handler(NULL, NULL);
    button_set_confirm_on_release_handler(NULL, NULL);
    button_set_confirm_on_longpush_handler(NULL, NULL);
    return (ret_stat);
}

bool confirm(ButtonRequestType type, const char *request_title,
             const char *request_body, ...) {
    m_button_request_acked = false;

    va_list vl;
    va_start(vl, request_body);
    vsnprintf(strbuf, BODY_CHAR_MAX, request_body, vl);
    va_end(vl);

    /* Send button request */
    ButtonRequest resp;
    memset(&resp, 0, sizeof(ButtonRequest));
    resp.has_code = true;
    resp.code = type;
    msg_write(MessageType_MessageType_ButtonRequest, &resp);

    bool ret =
        confirm_helper(request_title, strbuf, &layout_standard_notification);
    memzero(strbuf, sizeof(strbuf));
    return ret;
}

bool confirm_with_custom_button_request(ButtonRequest *button_request,
                                        const char *request_title,
                                        const char *request_body, ...) {
    m_button_request_acked = false;

    va_list vl;
    va_start(vl, request_body);
    vsnprintf(strbuf, BODY_CHAR_MAX, request_body, vl);
    va_end(vl);

    /* Send button request */
    msg_write(MessageType_MessageType_ButtonRequest, button_request);

    bool ret =
        confirm_helper(request_title, strbuf, &layout_standard_notification);
    memzero(strbuf, sizeof(strbuf));
    return ret;
}

bool confirm_with_custom_layout(layout_notification_t layout_notification_func,
                                ButtonRequestType type,
                                const char *request_title,
                                const char *request_body, ...) {
    m_button_request_acked = false;

    va_list vl;
    va_start(vl, request_body);
    vsnprintf(strbuf, BODY_CHAR_MAX, request_body, vl);
    va_end(vl);

    /* Send button request */
    ButtonRequest resp;
    memset(&resp, 0, sizeof(ButtonRequest));
    resp.has_code = true;
    resp.code = type;
    msg_write(MessageType_MessageType_ButtonRequest, &resp);

    bool ret = confirm_helper(request_title, strbuf, layout_notification_func);
    memzero(strbuf, sizeof(strbuf));
    return ret;
}

bool confirm_without_button_request(const char *request_title,
                                    const char *request_body, ...) {
    m_button_request_acked = true;

    va_list vl;
    va_start(vl, request_body);
    vsnprintf(strbuf, BODY_CHAR_MAX, request_body, vl);
    va_end(vl);

    bool ret =
        confirm_helper(request_title, strbuf, &layout_standard_notification);
    memzero(strbuf, sizeof(strbuf));
    return ret;
}

bool review(ButtonRequestType type, const char *request_title,
            const char *request_body, ...) {
    m_button_request_acked = false;

    va_list vl;
    va_start(vl, request_body);
    vsnprintf(strbuf, BODY_CHAR_MAX, request_body, vl);
    va_end(vl);

    /* Send button request */
    ButtonRequest resp;
    memset(&resp, 0, sizeof(ButtonRequest));
    resp.has_code = true;
    resp.code = type;
    msg_write(MessageType_MessageType_ButtonRequest, &resp);

    (void)confirm_helper(request_title, strbuf, &layout_standard_notification);
    memzero(strbuf, sizeof(strbuf));
    return true;
}

bool review_without_button_request(const char *request_title,
                                   const char *request_body, ...) {
    m_button_request_acked = true;

    va_list vl;
    va_start(vl, request_body);
    vsnprintf(strbuf, BODY_CHAR_MAX, request_body, vl);
    va_end(vl);

    (void)confirm_helper(request_title, strbuf, &layout_standard_notification);
    memzero(strbuf, sizeof(strbuf));
    return true;
}