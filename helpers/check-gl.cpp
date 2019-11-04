/*
 * Copyright © 2019 Collabora Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <iostream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <set>

#include <X11/Xlib.h>
#include <GL/gl.h>
#include <GL/glx.h>

extern "C" {
#include <getopt.h>
};

const int WIDTH = 200;
const int HEIGHT = 200;

static const char *argv0;

class HelloTriangleGLApplication {
public:
    HelloTriangleGLApplication(bool visible)
      : m_visible(visible),
        m_display(nullptr),
        m_window(0),
        m_context(0)
    {
    }

    void run() {
        initGL();
        mainLoop();
        cleanup();
    }

private:
    bool m_visible;
    Display *m_display;
    Window m_window;
    GLXContext m_context;

    void initGL() {
        m_display = XOpenDisplay(nullptr);
        if (!m_display)
          {
            throw std::runtime_error("Unable to open display");
          }

        makeWindow();

        if (m_visible)
          XMapWindow(m_display, m_window);

        glXMakeCurrent(m_display, m_window, m_context);
    }

    void drawTriangle()
    {
        //clear color and depth buffer
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();

        // Make it red
        glColor3f(1.0f,0.0f,0.0f);

        glBegin(GL_TRIANGLES);
        glVertex3f(-0.8f,-0.8f,0.0f);
        glVertex3f(0.8f,-0.8f, 0.0f);
        glVertex3f(0.0f, 0.6f,0.0f);
        glEnd();

        glEndList();

        glEnable(GL_NORMALIZE);
    }

    void makeWindow()
    {
       int attribs[64];
       int i = 0;

       int scrnum;
       XSetWindowAttributes attr;
       unsigned long mask;
       Window root;
       XVisualInfo *visinfo;

       /* Singleton attributes. */
       attribs[i++] = GLX_RGBA;
       attribs[i++] = GLX_DOUBLEBUFFER;

       /* Key/value attributes. */
       attribs[i++] = GLX_RED_SIZE;
       attribs[i++] = 1;
       attribs[i++] = GLX_GREEN_SIZE;
       attribs[i++] = 1;
       attribs[i++] = GLX_BLUE_SIZE;
       attribs[i++] = 1;
       attribs[i++] = GLX_DEPTH_SIZE;
       attribs[i++] = 1;

       attribs[i++] = None;

       scrnum = DefaultScreen(m_display);
       root = RootWindow(m_display, scrnum);

       visinfo = glXChooseVisual(m_display, scrnum, attribs);
       if (!visinfo)
        {
          throw std::runtime_error("Error: couldn't get an RGB, Double-buffered visual");
          exit(1);
        }

       /* window attributes */
       attr.background_pixel = 0;
       attr.border_pixel = 0;
       attr.colormap = XCreateColormap(m_display, root, visinfo->visual, AllocNone);
       attr.event_mask = StructureNotifyMask | ExposureMask | KeyPressMask;
       mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;

       m_window = XCreateWindow( m_display, root, 0, 0, WIDTH, HEIGHT,
                            0, visinfo->depth, InputOutput,
                            visinfo->visual, mask, &attr );

       /* set hints and properties */
         {
            XSizeHints sizehints;
            sizehints.x = 0;
            sizehints.y = 0;
            sizehints.width  = WIDTH;
            sizehints.height = HEIGHT;
            sizehints.flags = USSize | USPosition;
            XSetNormalHints(m_display, m_window, &sizehints);
            XSetStandardProperties(m_display, m_window, "check-gl", "check-gl",
                                    None, (char **)NULL, 0, &sizehints);
         }

       m_context = glXCreateContext(m_display, visinfo, NULL, True );
       if (!m_context)
        {
          throw std::runtime_error("Error: glXCreateContext failed");
        }

       XFree(visinfo);
    }

    void mainLoop() {
        for (int i = 0; i < (m_visible ? 10000 : 10); ++i) {
          drawFrame();
        }
    }

    void cleanup() {
        if (m_display) {
          glXMakeCurrent(m_display, None, nullptr);
          if (m_context) {
              glXDestroyContext(m_display, m_context);
          }
          if (m_window) {
              XDestroyWindow(m_display, m_window);
          }
          XCloseDisplay(m_display);
        }
    }

    void drawFrame() {
        drawTriangle();

        glXSwapBuffers(m_display, m_window);
    }

};

enum {
    OPTION_HELP = 1,
    OPTION_VERSION,
    OPTION_VISIBLE,
};

static struct option long_options[] = {
    { "help", no_argument, NULL, OPTION_HELP },
    { "version", no_argument, NULL, OPTION_VERSION },
    { "visible", no_argument, NULL, OPTION_VISIBLE },
    { NULL, 0, NULL, 0 }
};

static void usage(int code) __attribute__((__noreturn__));
static void usage(int code) {
    std::ostream& stream = (code == EXIT_SUCCESS ? std::cout : std::cerr);

    stream << "Usage: " << argv0 << " [OPTIONS]" << std::endl;
    stream << "Options:" << std::endl;
    stream << "--help\t\tShow this help and exit" << std::endl;
    stream << "--visible\tMake test window visible" << std::endl;
    stream << "--version\tShow version and exit" << std::endl;
    std::exit(code);
}

int main(int argc, char** argv) {
    int opt;
    bool visible = false;

    argv0 = argv[0];

    while ((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
      switch (opt) {
        case OPTION_HELP:
          usage(0);
          break;  // not reached

        case OPTION_VERSION:
          /* Output version number as YAML for machine-readability,
           * inspired by `ostree --version` and `docker version` */
          std::cout << argv[0] << ":" << std::endl
                    << " Package: steam-runtime-tools" << std::endl
                    << " Version: " << VERSION << std::endl;
          return EXIT_SUCCESS;

        case OPTION_VISIBLE:
          visible = true;
          break;

        case '?':
        default:
          usage(2);
          break;  // not reached
      }
    }

    HelloTriangleGLApplication app(visible);

    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

