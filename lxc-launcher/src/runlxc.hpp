/*
 * Copyright (c) 2019,2020 Panasonic Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in 
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef RUNLXC_HPP
#define RUNLXC_HPP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <sys/reboot.h>
#include <lxc/lxccontainer.h>

#include <cstdio>

#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include <ilm/ilm_control.h>
#include <ilm/ilm_input.h>

#define AGL_FATAL(fmt, ...) fatal("ERROR: " fmt "\n", ##__VA_ARGS__)
#define AGL_WARN(fmt, ...) warn("WARNING: " fmt "\n", ##__VA_ARGS__)
#define AGL_DEBUG(fmt, ...) debug("DEBUG: " fmt "\n", ##__VA_ARGS__)
#define AGL_TRACE(file,line) debug("%s:%d\n", file,line);

void fatal (const char* format, ...);
void warn (const char* format, ...);
void debug (const char* format, ...);

class RunLXC;

struct ILMScreen
{
public:
  ILMScreen(void) {};
  ILMScreen(std::string& name, t_ilm_uint id, t_ilm_uint w, t_ilm_uint h)
    : m_name(name), m_id(id), m_width(w), m_height(h) {};

  std::string m_name;
  t_ilm_uint m_id;
  t_ilm_uint m_width;
  t_ilm_uint m_height;
};

struct Output
{
public:
  Output(void) {};
  Output(const std::string& name, t_ilm_uint id);

  std::string m_name;           // name of display
  t_ilm_uint m_layer_id;        // ilm layer id specified by config

  t_ilm_uint m_surface_id = 0;  // surface of nested weston (wayland-backend)
};

struct Storage
{
  Storage(const std::string& src, const std::string& dst)
    : m_src(src), m_dst(dst) {};
  
public:
  std::string m_src;
  std::string m_dst;
};

class ILMControl
{
public:
  ILMControl(RunLXC *runlxc);
  ~ILMControl(void);

  void start(void);

  static void notify_surface_cb_static (t_ilm_uint id, struct ilmSurfaceProperties* prop, t_ilm_notification_mask mask);

  static void notify_ilm_cb_static (ilmObjectType object, t_ilm_uint id, t_ilm_bool created, void* user_data);

  void create_layer (const std::string& display, t_ilm_uint id);

private:
  RunLXC *m_runlxc;
  bool m_cb_registered;

  void configure_ilm_surface (t_ilm_uint id, t_ilm_uint width, t_ilm_uint height);
  void notify_surface_cb (t_ilm_uint id, struct ilmSurfaceProperties* prop, t_ilm_notification_mask mask);
  void notify_ilm_cb (ilmObjectType object, t_ilm_uint id, t_ilm_bool created);

  std::map<std::string, ILMScreen> m_screens;
  std::map<t_ilm_uint, Output> m_created_surfaces;
};

class Container
{
public:
  Container(const std::string& name);

  void launch(ILMControl *ilmc);
  void put(void);

  void add_output(const std::string& name, t_ilm_uint id);
  Output* next_output(t_ilm_uint id);
  void clear_output(t_ilm_uint id);

  void add_storage(const std::string& src, const std::string& dst);

  const char* name(void) { return m_name.c_str(); }

  pid_t m_pid;                  // init_pid
  pid_t m_wait_pid;             // child process for wait(STOPPED)

  bool m_reboot;         // if true, reboot the system when container is stopped

private:
  std::string m_name;           // container name
  std::vector<Output> m_outputs;
  std::vector<Storage> m_storages;
  struct lxc_container *m_lxc;
};

class RunLXC
{
public:
  RunLXC(void);

  void start(void);
  Container* find_container (pid_t pid);

  pthread_mutex_t m_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t m_cond = PTHREAD_COND_INITIALIZER;

private:
  std::vector<Container> m_containers;

  ILMControl* m_ilm_c;

  int parse_config(const char* file);

  void do_loop(volatile sig_atomic_t& e_flag);
};

#endif  // RUNLXC_HPP
