#pragma once

namespace nascent {
    float smoothstep(double begin, double end, double t);
    
    inline int positive_modulo(int i, int n) {
        return (i % n + n) % n;
    }
}
