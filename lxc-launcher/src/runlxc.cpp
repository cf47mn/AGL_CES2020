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
#include "cpptoml/cpptoml.h"

#include "runlxc.hpp"

#define RUNLXC_CONFIG_PATH "/etc/lxc"
#define RUNLXC_CONFIG "runlxc.conf"

#define PROCPS_BUFSIZE 4096

void fatal(const char* format, ...)
{
  va_list va_args;
  va_start(va_args, format);
  vfprintf(stderr, format, va_args);
  va_end(va_args);

  exit(EXIT_FAILURE);
}

void warn(const char* format, ...)
{
  va_list va_args;
  va_start(va_args, format);
  vfprintf(stderr, format, va_args);
  va_end(va_args);
}

void debug(const char* format, ...)
{
  va_list va_args;
  va_start(va_args, format);
  vfprintf(stderr, format, va_args);
  va_end(va_args);
}

/*
 *
 * signal handler
 *
 */
volatile sig_atomic_t e_flag = 0;

static void sigterm_handler (int signum)
{
  e_flag = 1;
}

static void init_signal (void)
{
  struct sigaction act, info;

  /* Setup signal for SIGTERM */
  if (!sigaction(SIGTERM, NULL, &info)) {
    if (info.sa_handler == SIG_IGN) {
      AGL_DEBUG("SIGTERM being ignored.");
    } else if (info.sa_handler == SIG_DFL) {
      AGL_DEBUG("SIGTERM being defaulted.");
    }
  }

  act.sa_handler = &sigterm_handler;
  if (sigemptyset(&act.sa_mask) != 0) {
    AGL_FATAL("Cannot initialize sigaction");
  }
  act.sa_flags = 0;

  if (sigaction(SIGTERM, &act, &info) != 0) {
    AGL_FATAL("Cannot register signal handler for SIGTERM");
  }
}

/*
 *
 * Output of container
 *   name: connector name (e.g. HDMI-A-1, HDMI-A-2, remote-1)
 *   id: ilm layer id
 *
 */
Output::Output (const std::string& name, t_ilm_uint id) 
  : m_name(name), m_layer_id(id), m_surface_id(0)
{
  AGL_DEBUG("  new output: name=[%s], layer=%d", m_name.c_str(), m_layer_id);
}

/*
 *
 * Container (Guest)
 *   name: container name
 *
 */
Container::Container (const std::string& name) : m_name(name)
{
  AGL_DEBUG("name = [%s]", m_name.c_str());
}

void Container::launch (ILMControl *ilmc)
{
  AGL_DEBUG("Launch LXC container [name=%s, reboot=%d]", m_name.c_str(), m_reboot);

  pid_t pid;

  pid = fork();
  if (pid < 0) {
    AGL_FATAL("fork() failed");
  }

   m_lxc = lxc_container_new(m_name.c_str(), NULL);
  if (!m_lxc) {
    AGL_FATAL("Cannot create container [%s]", m_name.c_str());
  }

  if (!pid) {
    bool ret;

    // child process
    AGL_DEBUG("MONITOR: start");

    // wait for RUNNING
    ret = m_lxc->wait(m_lxc, "RUNNING", 10);
    if (!ret) {
      AGL_FATAL("container[%s] didn't start.", m_name.c_str());
      _exit(EXIT_FAILURE);
    }
    AGL_DEBUG("MONITOR: RUNNING [%s]", m_name.c_str());

    // wait for container STOPPED
    ret = m_lxc->wait(m_lxc, "STOPPED", -1);
    if (!ret) {
      AGL_FATAL("container[%s] failed to catch STOPPED.", m_name.c_str());
      _exit(EXIT_FAILURE);
    }

    AGL_DEBUG("MONITOR: STOPPED [%s]", m_name.c_str());
    _exit(EXIT_SUCCESS);
  }

  m_wait_pid = pid;

  // parent process
  //   container's daemonized is enabled
  m_lxc = lxc_container_new(m_name.c_str(), NULL);
  if (!m_lxc) {
    AGL_FATAL("Cannot create container [%s]", m_name.c_str());
  }

  if (!m_lxc->is_running(m_lxc)) {
    if (!m_lxc->start(m_lxc, 0, NULL)) {
      AGL_FATAL("Cannot start container [%s]", m_name.c_str());
    }
  } else {
    AGL_DEBUG("Container[%s] is already running.", m_name.c_str());
  }

  // add extra storages if needed (fake hotplug)
  for (auto& storage : m_storages) {
    bool ret = m_lxc->add_device_node(m_lxc, storage.m_src.c_str(), storage.m_dst.c_str());
    if (!ret) {
      AGL_FATAL("Container[%s] fails to add device [%s,%s]", m_name.c_str(),
                storage.m_src.c_str(), storage.m_dst.c_str());
    }
    AGL_DEBUG("Container[%s] add device [%s, %s]", m_name.c_str(), 
              storage.m_src.c_str(), storage.m_dst.c_str());
  }

  m_pid = m_lxc->init_pid(m_lxc);
  AGL_DEBUG("Container[%s] init_pid=%d, wait_pid=%d", m_name.c_str(), m_pid, m_wait_pid);

  AGL_DEBUG("CHECK [%s,%p], pid=%d", this->name(), this, this->m_pid);

  for (auto& output : m_outputs) {
    ilmc->create_layer(output.m_name, output.m_layer_id);
  }
}

void Container::put (void)
{
  // force clear outputs
  for (auto& output : m_outputs) {
    output.m_surface_id = 0;
  }

  lxc_container_put(m_lxc);
  m_lxc = NULL;
}

void Container::add_output (const std::string& name, t_ilm_uint id)
{
  Output output(name, id);
  m_outputs.push_back(output);
}

Output* Container::next_output (t_ilm_uint id)
{
  for (auto itr = m_outputs.begin(); itr != m_outputs.end(); ++itr) {
    Output* output = &(*itr);
    if (output->m_surface_id == id ||
        output->m_surface_id == 0) {
      output->m_surface_id = id;
      return output;
    }
  }
  return nullptr;
}

void Container::clear_output (t_ilm_uint id)
{
  for (auto& output : m_outputs) {
    if (output.m_surface_id == id) {
      output.m_surface_id = 0;
    }
  }
}

void Container::add_storage (const std::string& src, const std::string& dst)
{
  Storage storage(src, dst);
  m_storages.push_back(storage);
}

/*
 *
 * TOML parser for config
 *
 */
int RunLXC::parse_config (const char *path)
{
  auto config = cpptoml::parse_file(path);

  if (config == nullptr) {
    AGL_DEBUG("cannot parse %s", path);
    return -1;
  }

  AGL_DEBUG("[%s] parsed", path);

  auto table_array = config->get_table_array("container");
  if (table_array == nullptr) {
    AGL_DEBUG("cannot find array of table, [[container]]");
    return -1;
  }

  for (const auto& table : *table_array) {
    // *table is a cpptoml::table

    auto name = table->get_as<std::string>("name");
    if (name->empty()) {
      AGL_FATAL("No name of container");
    }

    Container container(*name);

    container.m_reboot = *(table->get_as<int>("reboot"));

    auto screen_array = table->get_table_array("screen");
    for (const auto& screen : *screen_array) {
      std::string dpy_name = *(screen->get_as<std::string>("display"));
      if (dpy_name.empty()) {
        AGL_FATAL("No name of display in container:[%s]", dpy_name.c_str());
      }

      container.add_output(dpy_name, *(screen->get_as<t_ilm_uint>("layer")));
    }

    auto storage_array = table->get_table_array("storage");
    if (storage_array) {
      for (const auto& storage : *storage_array) {
        std::string src = *(storage->get_as<std::string>("src"));
        if (src.empty()) {
          AGL_FATAL("No src of storage in container:[%s]", src.c_str());
        }
        container.add_storage(src, *(storage->get_as<std::string>("dst")));
      }
    }

    m_containers.push_back(container);
  }

  return 0;
}

/*
 *
 * main loop
 *
 */
void RunLXC::do_loop (volatile sig_atomic_t& e_flag)
{
  pid_t w_pid = -1;
  int w_status;

  while (!e_flag) {
    pid_t pid = waitpid(w_pid, &w_status, 0);
    if (pid < 0) {
      if (errno == EINTR) {
        AGL_DEBUG("catch EINTR while waitpid()");
        continue;
      } else if (errno == ECHILD) {
        AGL_DEBUG("No child");
        break;
      }
    }

    AGL_DEBUG("child process(%d) is exited.", pid);

    for (auto& container : m_containers) {
      AGL_DEBUG("check %d", container.m_wait_pid);
      if (pid == container.m_wait_pid) {
        const char *name = container.name();

        if (WIFEXITED(w_status)) {
          AGL_DEBUG("[%s] terminated, return %d", name, WEXITSTATUS(w_status));
        } else if (WIFSIGNALED(w_status)) {
          AGL_DEBUG("%s terminated by signal %d", name, WTERMSIG(w_status));
        }

        // re-launch container or reboot system
        if (container.m_reboot) {
          AGL_DEBUG("rebooting by [%s]...", name);
          sync();
          int ret = reboot(RB_AUTOBOOT);
          if (ret) {
            AGL_DEBUG("reboot fail %d, %d", ret, errno);
          }
          break;
        } else {
          AGL_DEBUG("re-launch container [%s]", name);
          container.put();

          container.launch(m_ilm_c);
        }
      }
    }
  }

  if (e_flag) {
    /* parent killed by someone, so need to kill children */
    AGL_DEBUG("killpg(0, SIGTERM)");

    // shutdown all container
    killpg(0, SIGTERM);
  }
}

/*
 *
 * RunLXC
 *
 */
RunLXC::RunLXC (void)
{
  auto path = std::string(RUNLXC_CONFIG_PATH);
  path = path + "/" + RUNLXC_CONFIG;

  // parse config of runlxc
  if (parse_config(path.c_str())) {
    AGL_FATAL("Error in parse config");
  }

  m_ilm_c = new ILMControl(this);

  AGL_DEBUG("RunLXC created.");
}

void RunLXC::start (void)
{
  init_signal();

  // start LXC container
  for (auto& container : m_containers) {
    container.launch(m_ilm_c);
    ilm_commitChanges();
    sleep(1);
  }

  ilm_commitChanges();
  do_loop(e_flag);
}

/*
 * pid: compositor(guest)'s pid
 */
Container* RunLXC::find_container (pid_t pid)
{
  char filename[sizeof("/proc//stat") + sizeof (int) * 3];
  char buf[PROCPS_BUFSIZE];

  sprintf(filename, "/proc/%d/stat", pid);

  int fd = open(filename, O_RDONLY);
  if (fd < 0) {
    return nullptr;
  }

  int ret = read(fd, buf, PROCPS_BUFSIZE-1);
  close(fd);
  buf[ret > 0 ? ret : 0] = '\0';

  char *cp = strrchr(buf, ')');

  if (cp == NULL) {
    AGL_FATAL("fail to parse PROCPS");
  }

  char state;
  pid_t ppid;

  int n = sscanf(cp+2, "%c %u ", &state, &ppid);
  if (n != 2) {
    return nullptr;
  }

  for (auto& container: m_containers) {
    if (container.m_pid == ppid) {
      // FOUND
      return &container;
    }
  }

  return nullptr;
}

int main (int argc, const char* argv[])
{
  RunLXC runlxc;

  runlxc.start();

  return 0;
}
