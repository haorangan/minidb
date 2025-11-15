//
// Created by Haoran Gan on 11/15/25.
//

#pragma once

#include <cstdint>
#include <iostream>

struct Tuple {
    int32_t a;
    int32_t b;

    Tuple(int32_t a_ = 0, int32_t b_ = 0) : a(a_), b(b_) {}

    void Print() const {
        std::cout << "(" << a << ", " << b << ")";
    }
};
