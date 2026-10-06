#include "gpio_example.h"
#include "example_timer.h"

#include <applibs/log.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct GpioExample {
  int inputFd;
  int outputFd;
  ExampleTimer *timer;
  GPIO_Value_Type inactiveValue;
  GPIO_Value_Type stableValue;
  GPIO_Value_Type candidate;
  unsigned int consecutiveSamples;
  bool failed;
  GpioExampleCallback changed;
  void *context;
};

static void PollInput(int error, uint64_t expirations, void *context) {
  (void)expirations;
  GpioExample *example = context;
  if (example->failed) {
    return;
  }
  GPIO_Value_Type value = GPIO_Value_Low;
  if (error == 0 && GPIO_GetValue(example->inputFd, &value) == -1) {
    error = errno;
  }
  if (error != 0) {
    example->failed = true;
    if (ExampleTimer_Set(example->timer, 0, 0) == -1) {
      Log_Debug("Cannot stop GPIO polling: %s\n", strerror(errno));
    }
    example->changed(error, example->stableValue, example->context);
    return;
  }
  // Three consecutive samples, not elapsed timer expirations, debounce an input.
  if (value != example->candidate) {
    example->candidate = value;
    example->consecutiveSamples = 1;
  } else if (example->consecutiveSamples < 3) {
    ++example->consecutiveSamples;
  }
  if (example->consecutiveSamples == 3 && value != example->stableValue) {
    example->stableValue = value;
    example->changed(0, value, example->context);
  }
}

GpioExample *GpioExample_Start(EventLoop *loop, GPIO_Id inputPin, GPIO_Id outputPin, GPIO_Value_Type inactiveValue, GpioExampleCallback changed, void *context) {
  if (loop == NULL || changed == NULL || inputPin < 0 || outputPin < 0 || inputPin == outputPin || inactiveValue > GPIO_Value_High) {
    errno = EINVAL;
    return NULL;
  }
  GpioExample *example = calloc(1, sizeof(*example));
  if (example == NULL) {
    return NULL;
  }
  example->inputFd = -1;
  example->outputFd = -1;
  example->inactiveValue = inactiveValue;
  example->changed = changed;
  example->context = context;
  example->inputFd = GPIO_OpenAsInput(inputPin);
  if (example->inputFd == -1) {
    goto failure;
  }
  example->outputFd = GPIO_OpenAsOutput(outputPin, GPIO_OutputMode_PushPull, inactiveValue);
  if (example->outputFd == -1 || GPIO_GetValue(example->inputFd, &example->stableValue) == -1) {
    goto failure;
  }
  example->candidate = example->stableValue;
  example->timer = ExampleTimer_Create(loop, PollInput, example);
  if (example->timer == NULL || ExampleTimer_Set(example->timer, 20, 20) == -1) {
    goto failure;
  }
  return example;

failure: {
  int error = errno;
  GpioExample_Close(example);
  errno = error;
  return NULL;
}
}

int GpioExample_SetOutput(GpioExample *example, GPIO_Value_Type value) {
  if (example == NULL || value > GPIO_Value_High) {
    errno = EINVAL;
    return -1;
  }
  return GPIO_SetValue(example->outputFd, value);
}

void GpioExample_Close(GpioExample *example) {
  if (example == NULL) {
    return;
  }
  ExampleTimer_Close(example->timer);
  if (example->outputFd != -1) {
    if (GPIO_SetValue(example->outputFd, example->inactiveValue) == -1) {
      Log_Debug("Cannot restore GPIO output: %s\n", strerror(errno));
    }
    if (close(example->outputFd) == -1) {
      Log_Debug("GPIO output close failed: %s\n", strerror(errno));
    }
  }
  if (example->inputFd != -1 && close(example->inputFd) == -1) {
    Log_Debug("GPIO input close failed: %s\n", strerror(errno));
  }
  free(example);
}
