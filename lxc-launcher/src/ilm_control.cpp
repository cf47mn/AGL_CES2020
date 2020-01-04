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

#include "runlxc.hpp"

static ILMControl *global;

/*
 *
 * Configure ilm surface
 *
 */
void ILMControl::configure_ilm_surface(t_ilm_uint id, t_ilm_uint width, t_ilm_uint height)
{
  AGL_DEBUG("ILMControl: surface (%d) configured: %d x %d", id, width, height);

  ilm_surfaceSetDestinationRectangle(id, 0, 0, width, height);
  ilm_surfaceSetSourceRectangle(id, 0, 0, width, height);
  ilm_surfaceSetVisibility(id, ILM_TRUE);

  Output& output = m_created_surfaces[id];

  ilm_layerAddSurface(output.m_layer_id, id);
  ilm_surfaceRemoveNotification(id);

  ilm_commitChanges();
  //pthread_cond_signal(&m_runlxc->m_cond);

}

/*
 *
 * Callback's of ilmControl
 *
 */
void ILMControl::notify_surface_cb (t_ilm_uint id, struct ilmSurfaceProperties* prop, t_ilm_notification_mask mask)
{
  if (mask & ILM_NOTIFICATION_CONFIGURED) {
    configure_ilm_surface(id, prop->origSourceWidth, prop->origSourceHeight);
  }
}

void ILMControl::notify_surface_cb_static (t_ilm_uint id, struct ilmSurfaceProperties* prop, t_ilm_notification_mask mask)
{
  ILMControl* c = static_cast<ILMControl*>(global);
  c->notify_surface_cb(id, prop, mask);
}

void ILMControl::notify_ilm_cb (ilmObjectType object, t_ilm_uint id, t_ilm_bool created)
{
  if (object == ILM_SURFACE) {
    struct ilmSurfaceProperties props;
    t_ilm_uint surface = id;

    ilm_getPropertiesOfSurface(surface, &props);
    pid_t pid = props.creatorPid;

    // find container
    Container* c = m_runlxc->find_container(pid);
    if (c == nullptr) {
      AGL_DEBUG("ILM notify: cannot find container (pid=%d)", pid);
    } else {
      AGL_DEBUG("ILM notify: container[%s], pid=%d", c->name(), pid);

      if (!created) {
        AGL_DEBUG("Compositor of [%s] (id=%d) has been destroyed.", c->name(), surface);

        // clear guest compositor
        c->clear_output(surface);
      } else {
        AGL_DEBUG("ivi surface (id=%d, pid=%d) is created.", surface, pid);

        // find new guest compositor
        Output* output = c->next_output(surface);
        if (output == nullptr) {
          AGL_DEBUG("???: no more uninitialized guest output.");
        } else {
          m_created_surfaces[surface] = *output;

          ilm_surfaceAddNotification(surface, notify_surface_cb_static);
          ilm_commitChanges();
          ilm_getPropertiesOfSurface(surface, &props);

          if ((props.origSourceWidth != 0) && (props.origSourceHeight != 0)) {
            // this surface is already configured
            AGL_DEBUG("surface (id=%d,pid=%d) is already configured", id, pid);
            configure_ilm_surface(id, props.origSourceWidth, props.origSourceHeight);
          }
        }
      }
    }
  } else if (object == ILM_LAYER) {
    t_ilm_uint layer = id;
    if (created) {
      AGL_DEBUG("ivi layer: %d created.", layer);
    } else {
      AGL_DEBUG("ivi layer: %d destroyed.", layer);
    }
  }
}

void ILMControl::notify_ilm_cb_static (ilmObjectType object, t_ilm_uint id, t_ilm_bool created, void *user_data)
{
  ILMControl *c = static_cast<ILMControl*>(user_data);
  c->notify_ilm_cb(object, id, created);
}

/*
 *
 * ILMControl
 *
 */
ILMControl::ILMControl(RunLXC *runlxc) 
  : m_runlxc(runlxc), m_cb_registered(false)
{
  // dirty hack: wait for weston readiness
  while (1) {
    int ret = std::system("/usr/bin/LayerManagerControl get screens > /dev/null 2>&1");
    if (!ret) break;
    AGL_DEBUG("wait for weston...");
    usleep(500000);
  }

  AGL_DEBUG("ILMControl:start");
  ilm_init();

  t_ilm_uint* screen_ids = NULL;
  t_ilm_uint num_screens = 0;

  ilm_getScreenIDs(&num_screens, &screen_ids);

  AGL_DEBUG("ILMControl:num_screens=%d",num_screens);

  for (int i = 0; i < num_screens; i++) {
    struct ilmScreenProperties props;
    t_ilm_uint id = 0;
    t_ilm_uint width = 0;
    t_ilm_uint height = 0;

    ilm_getPropertiesOfScreen(screen_ids[i], &props);

    std::string name(props.connectorName);

    AGL_DEBUG("ILMControl:connector=[%s], id=%d",name.c_str(), screen_ids[i]);

    ILMScreen screen(name, screen_ids[i],
                     props.screenWidth, props.screenHeight);

    m_screens[name] = screen;
  }

  free(screen_ids);

  global = this;

  AGL_DEBUG("ILMControl:end");
}

void ILMControl::create_layer (const std::string& display, t_ilm_uint id)
{
  ILMScreen screen = m_screens[display];

  t_ilm_layer render_order[1] = { id };

  AGL_DEBUG("ILMControl: create layer=%d to screen=%d,[%s]", id, screen.m_id, display.c_str());

  ilm_layerCreateWithDimension(&id, screen.m_width, screen.m_height);
  ilm_layerSetVisibility(id, ILM_TRUE);
  ilm_displaySetRenderOrder(screen.m_id, render_order, 1);
  ilm_commitChanges();

  if (!m_cb_registered) {
    ilm_registerNotification(notify_ilm_cb_static, this);
    m_cb_registered = true;
  }
}

ILMControl::~ILMControl(void) {
  m_screens.clear();
  ilm_unregisterNotification();
  ilm_destroy();
  AGL_DEBUG("ilm_destory().\n");
}
