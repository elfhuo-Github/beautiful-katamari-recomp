// uinput_pad - virtual Xbox 360 controller via /dev/uinput
//
// Creates a uinput device that identifies as "Xbox 360 Controller"
// (BUS_USB / 045e:028e / 0110) so that SDL3's built-in game controller
// database recognises it, then lets you drive it from stdin.
//
//   uinput_pad            -> creates the pad, prints "READY", then reads
//                            commands from stdin, one per line:
//                              A=1 B=0 LX=1234 UP=1
//                            An empty line is a no-op (keeps the state).
//                            EOF releases everything and exits.
//
//   uinput_pad --demo     -> creates the pad and cycles through a button
//                            every 2 seconds (for smoke tests).
//
// The device stays alive as long as this process runs.

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int fd;

static void emit(int type, int code, int value) {
  struct input_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = (unsigned short)type;
  ev.code = (unsigned short)code;
  ev.value = value;
  if (write(fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) {
    perror("write");
  }
}

static void sync_report(void) { emit(EV_SYN, SYN_REPORT, 0); }

struct pad_state {
  int buttons[16]; /* indexed by button code below */
  int axes[8];
};

enum {
  PB_A = 0,
  PB_B,
  PB_X,
  PB_Y,
  PB_LB,
  PB_RB,
  PB_START,
  PB_BACK,
  PB_GUIDE,
  PB_LS,
  PB_RS,
  PB_COUNT
};

static const int kButtonCodes[PB_COUNT] = {
    BTN_SOUTH, BTN_EAST,  BTN_NORTH, BTN_WEST,  BTN_TL,    BTN_TR,
    BTN_START, BTN_SELECT, BTN_MODE, BTN_THUMBL, BTN_THUMBR};

enum { AX_LX = 0, AX_LY, AX_LT, AX_RX, AX_RY, AX_RT };
static const int kAxisCodes[6] = {ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ};

/* hat is a single axis-ish value: -1 left, 0 centre, 1 right / up */
static int hat_x = 0, hat_y = 0;

static void apply_button(int idx, int value) {
  emit(EV_KEY, kButtonCodes[idx], value ? 1 : 0);
}

static void apply_hat(void) {
  emit(EV_ABS, ABS_HAT0X, hat_x);
  emit(EV_ABS, ABS_HAT0Y, hat_y);
}

static int parse_axis(const char *name, int *code) {
  if (!strcmp(name, "LX"))
    *code = AX_LX;
  else if (!strcmp(name, "LY"))
    *code = AX_LY;
  else if (!strcmp(name, "LT"))
    *code = AX_LT;
  else if (!strcmp(name, "RX"))
    *code = AX_RX;
  else if (!strcmp(name, "RY"))
    *code = AX_RY;
  else if (!strcmp(name, "RT"))
    *code = AX_RT;
  else
    return 0;
  return 1;
}

static int parse_button(const char *name, int *idx) {
  static const char *names[PB_COUNT] = {"A", "B", "X", "Y", "LB", "RB",
                                        "START", "BACK", "GUIDE", "LS", "RS"};
  for (int i = 0; i < PB_COUNT; i++) {
    if (!strcmp(name, names[i])) {
      *idx = i;
      return 1;
    }
  }
  return 0;
}

static void handle_line(char *line) {
  char *save = NULL;
  for (char *tok = strtok_r(line, " \t\r\n", &save); tok; tok = strtok_r(NULL, " \t\r\n", &save)) {
    char *eq = strchr(tok, '=');
    if (!eq)
      continue;
    *eq = 0;
    const char *name = tok;
    int value = atoi(eq + 1);
    int idx, code;
    if (parse_button(name, &idx)) {
      apply_button(idx, value);
    } else if (!strcmp(name, "UP")) {
      hat_y = value ? -1 : 0;
      apply_hat();
    } else if (!strcmp(name, "DOWN")) {
      hat_y = value ? 1 : 0;
      apply_hat();
    } else if (!strcmp(name, "LEFT")) {
      hat_x = value ? -1 : 0;
      apply_hat();
    } else if (!strcmp(name, "RIGHT")) {
      hat_x = value ? 1 : 0;
      apply_hat();
    } else if (parse_axis(name, &code)) {
      emit(EV_ABS, kAxisCodes[code], value);
    } else {
      fprintf(stderr, "uinput_pad: unknown token '%s'\n", name);
      continue;
    }
  }
  sync_report();
  fflush(stdout);
}

int main(int argc, char **argv) {
  fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (fd < 0) {
    perror("open /dev/uinput");
    return 1;
  }

  ioctl(fd, UI_SET_EVBIT, EV_SYN);
  ioctl(fd, UI_SET_EVBIT, EV_KEY);
  ioctl(fd, UI_SET_EVBIT, EV_ABS);

  const int keys[] = {BTN_SOUTH, BTN_EAST,  BTN_NORTH,    BTN_WEST,
                       BTN_TL,    BTN_TR,   BTN_SELECT,   BTN_START,
                       BTN_MODE,  BTN_THUMBL, BTN_THUMBR};
  for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    ioctl(fd, UI_SET_KEYBIT, keys[i]);

  const int axes[] = {ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ, ABS_HAT0X, ABS_HAT0Y};
  for (unsigned i = 0; i < sizeof(axes) / sizeof(axes[0]); i++)
    ioctl(fd, UI_SET_ABSBIT, axes[i]);

  struct uinput_setup setup;
  memset(&setup, 0, sizeof(setup));
  setup.id.bustype = BUS_USB;
  setup.id.vendor = 0x045E;
  setup.id.product = 0x028E;
  setup.id.version = 0x0110;
  snprintf(setup.name, sizeof(setup.name), "Xbox 360 Controller");
  if (ioctl(fd, UI_DEV_SETUP, &setup) < 0)
    perror("UI_DEV_SETUP");
  if (ioctl(fd, UI_DEV_CREATE) < 0) {
    perror("UI_DEV_CREATE");
    return 1;
  }

  /* neutral state */
  apply_hat();
  for (int i = 0; i < 6; i++)
    emit(EV_ABS, kAxisCodes[i], 0);
  sync_report();

  printf("READY\n");
  fflush(stdout);

  if (argc > 1 && !strcmp(argv[1], "--demo")) {
    const char *cycle[] = {"A=1", "B=1", "X=1", "Y=1", "START=1", "A=0 B=0 X=0 Y=0 START=0"};
    for (int i = 0;; i++) {
      sleep(2);
      handle_line((char *)cycle[i % 7]);
    }
  }

  char buf[512];
  while (fgets(buf, sizeof(buf), stdin)) {
    handle_line(buf);
  }

  if (argc > 1 && !strcmp(argv[1], "--keep")) {
    /* stdin closed: keep the device registered so the game keeps seeing it. */
    for (;;)
      sleep(3600);
  }

  for (int i = 0; i < PB_COUNT; i++)
    if (kButtonCodes[i])
      apply_button(i, 0);
  for (int i = 0; i < 6; i++)
    emit(EV_ABS, kAxisCodes[i], 0);
  sync_report();
  usleep(200000);
  ioctl(fd, UI_DEV_DESTROY);
  close(fd);
  return 0;
}
