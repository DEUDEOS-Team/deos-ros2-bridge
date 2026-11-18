#pragma once

#include <deos/deos_frame.h>

struct deos_header_t {
    uint8_t destination;
    uint8_t source;
    uint8_t priority;
    uint8_t version;
    uint8_t message_class;
    uint8_t service;
    uint8_t command;
    uint8_t sequence;
};
