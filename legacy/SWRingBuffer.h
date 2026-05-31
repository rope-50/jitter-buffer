#ifndef SWRINGBUFFER_H
#define SWRINGBUFFER_H

#include <stdio.h>
#include <cstdlib>
#include <iostream>
#include <string.h>
#include <atomic>
class SWRingBuffer
{
public:
    int bufferSize;
    std::atomic<int> head;
    std::atomic<int> tail;
    int framesPerBurst;
    int burstIndex;
    bool isActive = false;
    int bytesPerBurst;
    int16_t *buffer = NULL;
    int16_t *udpBuffer = NULL;
    SWRingBuffer(int bufferSize, int framesPerBurst);
    ~SWRingBuffer();
    void append(int16_t *burst);
    int16_t *getRenderBuffer();
    int16_t *read();
    int16_t *getUDPBuffer(int size);
    bool checkActive();
    void reset();
    
    // Adaptive buffer management for low-latency output with different devices
    int getAvailableSamples() const;
    float getFillRatio() const;
    bool needsResampling() const;
};

#endif // SWRINGBUFFER_H
