#pragma once

struct Area {
    float* ptr;
    float* end;
    int step;
    float* start;

    Area()
        : ptr(nullptr), end(nullptr), step(0) {}
    Area(float *ptr, int num_samples, int step)
        : ptr(ptr), end(ptr + num_samples * step), step(step), start(ptr) {}

    float& operator*() const {
        return *ptr;
    }
    bool operator==(float* const ptr_2) const {
        return ptr == ptr_2;
    }
    bool operator!=(float* const ptr_2) const {
        return ptr != ptr_2;
    }
    bool operator<(float* const ptr_2) const {
        return ptr < ptr_2;
    }
    bool operator>(float* const ptr_2) const {
        return ptr > ptr_2;
    }
    bool operator<=(float* const ptr_2) const {
        return ptr <= ptr_2;
    }
    bool operator>=(float* const ptr_2) const {
        return ptr >= ptr_2;
    }
    Area& operator++() {
        ptr += step;
        return *this;
    }
    Area operator++(int) {
        Area copy = *this;
        ptr += step;
        return copy;
    }
    Area& operator+=(int steps) {
        ptr += steps * step;
        return *this;
    }
    Area& operator-=(int steps) {
        ptr -= steps * step;
        return *this;
    }
    Area operator+(int steps) {
        Area copy = *this;
        copy += steps;
        return copy;
    }
    Area operator-(int steps) {
        Area copy = *this;
        copy -= steps;
        return copy;
    }

    static int copy_over(Area area_in, Area area_out) {
        float* area_in_start = area_in.ptr;
        while (area_in < area_in.end && area_out < area_out.end) {
            *area_out++ = *area_in++;
        }
        return (int)((area_in.ptr - area_in_start) / area_in.step);
    }

    int size() {
        return (end - ptr) / step;
    }
};