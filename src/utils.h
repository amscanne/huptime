/*
 * utils.h
 *
 * Copyright 2013 Adin Scannell <adin@scannell.ca>, all rights reserved.
 *
 * This file is part of Huptime.
 *
 * Huptime is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Huptime is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Huptime.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HUPTIME_UTILS_H
#define HUPTIME_UTILS_H

#include <unistd.h>
#include <sys/types.h>

const char** read_nul_sep(const char* filename);
const char* read_link(const char* filename);

pid_t* get_tasks(void);

#endif
