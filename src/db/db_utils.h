#pragma once
#include "common/utils.h"
#include "common/log.h"

#define DB_DEBUG(fmt, ...) ZJC_DEBUG("[db]" fmt, ## __VA_ARGS__)
#define DB_INFO(fmt, ...) ZJC_INFO("[db]" fmt, ## __VA_ARGS__)
#define DB_WARN(fmt, ...) ZJC_WARN("[db]" fmt, ## __VA_ARGS__)
#define DB_ERROR(fmt, ...) ZJC_ERROR("[db]" fmt, ## __VA_ARGS__)

namespace zjchain {

namespace db {

static const char kDbFieldLinkLetter = '\x01';


}

}
