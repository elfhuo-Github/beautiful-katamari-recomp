/*
 * Tiny XTest based input injector (xdotool is not installed on this box).
 *
 *   xtest_input key  <keysym> [hold_ms]     e.g.  xtest_input key Return 120
 *   xtest_input hold <keysym> <down_ms>
 *   xtest_input mouse <x> <y> <button> [hold_ms]
 *   xtest_input focus                        print the focused window
 *
 * Keysyms are resolved with XStringToKeysym(), so names like "Return",
 * "space", "a", "Left", "KP_Enter" work.
 */
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>

/* Declared locally: the XTest development headers are not installed. */
typedef int (*XTestFakeKeyEventFn)(Display *, unsigned int, int, unsigned long);
typedef int (*XTestFakeButtonEventFn)(Display *, unsigned int, int, unsigned long);
typedef int (*XTestFakeMotionEventFn)(Display *, int, int, int, unsigned long);

static void *xtst;

static void die(const char *msg)
{
    fprintf(stderr, "xtest_input: %s\n", msg);
    exit(1);
}

int main(int argc, char **argv)
{
    Display *dpy;
    if (argc < 2)
        die("usage: xtest_input {key|hold|mouse|focus} ...");

    dpy = XOpenDisplay(NULL);
    if (!dpy)
        die("cannot open display");

    if (!strcmp(argv[1], "focus")) {
        Window w = None;
        int rev = 0;
        XGetInputFocus(dpy, &w, &rev);
        printf("focus window: 0x%lx\n", (unsigned long)w);
        return 0;
    }

    /* setfocus <window_id>  -- force X input focus onto a window */
    if (!strcmp(argv[1], "setfocus")) {
        if (argc < 3)
            die("setfocus <window_id>");
        Window w = (Window)strtoul(argv[2], NULL, 0);
        XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
        XFlush(dpy);
        usleep(150 * 1000);
        Window f = None;
        int rev = 0;
        XGetInputFocus(dpy, &f, &rev);
        printf("requested focus 0x%lx -> now 0x%lx\n", (unsigned long)w,
               (unsigned long)f);
        return 0;
    }

    /* find <wm_class_substring> -- print window ids whose WM_CLASS matches */
    if (!strcmp(argv[1], "find")) {
        if (argc < 3)
            die("find <wm_class_substring>");
        Window root = DefaultRootWindow(dpy);
        Window rr, parent, *kids = NULL;
        unsigned int nkids = 0;
        if (!XQueryTree(dpy, root, &rr, &parent, &kids, &nkids))
            die("XQueryTree failed");
        for (unsigned int i = 0; i < nkids; i++) {
            XClassHint hint;
            char *cls = NULL;
            if (XGetClassHint(dpy, kids[i], &hint) && hint.res_name) {
                cls = hint.res_name;
                if (strstr(cls, argv[2])) {
                    char *nm = NULL;
                    XFetchName(dpy, kids[i], &nm);
                    printf("0x%lx  class=%s/%s  name=%s\n", (unsigned long)kids[i],
                           hint.res_name, hint.res_class ? hint.res_class : "?",
                           nm ? nm : "(null)");
                    if (nm)
                        XFree(nm);
                }
                XFree(hint.res_name);
                if (hint.res_class)
                    XFree(hint.res_class);
            }
        }
        if (kids)
            XFree(kids);
        return 0;
    }

    xtst = dlopen("libXtst.so.6", RTLD_NOW);
    if (!xtst)
        die("cannot dlopen libXtst.so.6");
    XTestFakeKeyEventFn fake_key =
        (XTestFakeKeyEventFn)dlsym(xtst, "XTestFakeKeyEvent");
    XTestFakeButtonEventFn fake_button =
        (XTestFakeButtonEventFn)dlsym(xtst, "XTestFakeButtonEvent");
    XTestFakeMotionEventFn fake_motion =
        (XTestFakeMotionEventFn)dlsym(xtst, "XTestFakeMotionEvent");
    if (!fake_key || !fake_button || !fake_motion)
        die("cannot resolve XTest symbols");

    if (!strcmp(argv[1], "key")) {
        if (argc < 3)
            die("key <keysym> [hold_ms]");
        KeySym ks = XStringToKeysym(argv[2]);
        if (ks == NoSymbol)
            die("unknown keysym");
        unsigned int kc = XKeysymToKeycode(dpy, ks);
        if (!kc)
            die("keysym has no keycode");
        long hold = argc > 3 ? atol(argv[3]) : 80;
        fake_key(dpy, kc, True, 0);
        XFlush(dpy);
        usleep(hold * 1000);
        fake_key(dpy, kc, False, 0);
        XFlush(dpy);
        printf("pressed %s (keycode %u) for %ld ms\n", argv[2], kc, hold);
        return 0;
    }

    if (!strcmp(argv[1], "hold")) {
        if (argc < 4)
            die("hold <keysym> <down_ms>");
        KeySym ks = XStringToKeysym(argv[2]);
        unsigned int kc = XKeysymToKeycode(dpy, ks);
        long hold = atol(argv[3]);
        fake_key(dpy, kc, True, 0);
        XFlush(dpy);
        usleep(hold * 1000);
        fake_key(dpy, kc, False, 0);
        XFlush(dpy);
        return 0;
    }

    if (!strcmp(argv[1], "mouse")) {
        if (argc < 5)
            die("mouse <x> <y> <button> [hold_ms]");
        int x = atoi(argv[2]), y = atoi(argv[3]);
        unsigned int b = (unsigned int)atoi(argv[4]);
        long hold = argc > 5 ? atol(argv[5]) : 80;
        fake_motion(dpy, -1, x, y, 0);
        XFlush(dpy);
        usleep(40 * 1000);
        fake_button(dpy, b, True, 0);
        XFlush(dpy);
        usleep(hold * 1000);
        fake_button(dpy, b, False, 0);
        XFlush(dpy);
        printf("clicked %u at %d,%d\n", b, x, y);
        return 0;
    }

    die("unknown command");
    return 1;
}
