#include "SWRingBuffer.h"

SWRingBuffer::SWRingBuffer(int bufferSize, int framesPerBurst)
{
    tail.store(0);
    head.store(0);
    burstIndex = 0;
    this->framesPerBurst = framesPerBurst;
    this->bytesPerBurst = framesPerBurst * 4;
    this->bufferSize = bufferSize;
    this->udpBuffer = new int16_t[bufferSize + 1];
    buffer = new int16_t[bufferSize];
}

SWRingBuffer::~SWRingBuffer()
{
  delete[] udpBuffer;
  delete[] buffer;
}

void SWRingBuffer::append(int16_t *burst)
{
    int currentHead = head.load();
    memcpy(&buffer[currentHead], burst, 2 * 2 * framesPerBurst);
    head.store((currentHead + framesPerBurst * 2) % bufferSize);
}

int16_t *SWRingBuffer::getRenderBuffer()
{

    int prevHead = head.load();
    // printf("head: %d\n", head);
    head.store((prevHead + framesPerBurst * 2) % bufferSize);
    return &buffer[prevHead];
}

int16_t *SWRingBuffer::read()
{
    int prevTail = tail.load();
    tail.store((prevTail + framesPerBurst * 2) % bufferSize);
    return &buffer[prevTail];
}

/**
 * Create a redundant buffer from ring buffer to be sent through the network
 * */
int16_t *SWRingBuffer::getUDPBuffer(int nRedundancy)
{
    int currentTail = tail.load();
    int ringIndex;
    for (int i = 0; i < nRedundancy; i++)
    {
        ringIndex = (currentTail + (i * framesPerBurst * 2)) % bufferSize;
        // printf("Tail:%d  \tRI: %d\tBI: %d\t BPB:%d\n", tail, ringIndex, i * framesPerBurst * 2, bytesPerBurst);
        memcpy(&udpBuffer[i * framesPerBurst * 2], &buffer[ringIndex], bytesPerBurst);
    }
    udpBuffer[bufferSize] = burstIndex;
    burstIndex = (burstIndex + 1) % 750;
    // printf("\n");
    tail.store((currentTail + framesPerBurst * 2) % bufferSize);
    return udpBuffer;
}

bool SWRingBuffer::checkActive()
{
    // Activate at 25% fill (1 buffer) instead of 50% for lower latency
    // With 4-6 buffers total, this provides enough safety margin
    int currentHead = head.load();
    if (isActive == false && currentHead >= bufferSize / 4)
    {
        printf("RingBuffer is now active. head: %d, buffer size: %d (25%% threshold)\n", currentHead, bufferSize);
        isActive = true;
    }
    return isActive;
}

void SWRingBuffer::reset()
{
    isActive = false;
    head.store(0);
    tail.store(0);
}

// Adaptive buffer management methods for handling clock drift

int SWRingBuffer::getAvailableSamples() const
{
    // Calculate available samples in ring buffer
    // Read head and tail atomically to avoid race conditions
    int currentHead = head.load();
    int currentTail = tail.load();
    return (currentHead - currentTail + bufferSize) % bufferSize;
}

float SWRingBuffer::getFillRatio() const
{
    // Return buffer fill percentage (0.0 to 1.0)
    return (float)getAvailableSamples() / (float)bufferSize;
}

bool SWRingBuffer::needsResampling() const
{
    float ratio = getFillRatio();
    // If buffer is too full (>75%) or too empty (<25%), clock drift is detected
    return ratio > 0.75f || ratio < 0.25f;
}
