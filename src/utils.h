/*
 * utils.h
 */

#ifndef _UTILS_H_
#define _UTILS_H_

#include <unistd.h>
#include <sys/types.h>

const char** read_nul_sep(const char* filename);
const char* read_link(const char* filename);

pid_t* get_tasks(void);

#endif
