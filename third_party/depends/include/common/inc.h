#ifndef SECP256K1_INC_H
#define SECP256K1_INC_H

#ifdef BUILD_USER_LIB
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#elif defined(LINUX) || defined(linux) || defined(__linux) || defined(__linux__)
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <stddef.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <asm/uaccess.h>
#else
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#endif

#endif