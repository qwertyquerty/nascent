#pragma once

namespace nascent {
    inline int positive_modulo(int i, int n) {
        return (i % n + n) % n;
    }
}
