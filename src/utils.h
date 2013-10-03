/*
 * utils.h
 */

#ifndef _UTILS_H_
#define _UTILS_H_

const char** filter_nul_sep(const char* filename, const char* neg_filter, int extra);
const char* read_link(const char* filename);

#endif
