#pragma once

namespace nascent {
    class SongClock {
        protected:
        double anchor_position = 0;
        double anchor_time = 0;
        double last_reported_position = 0;
        bool has_report = false;

        public:
        double max_error = 20;
        double correction_rate = 0.1;
        double speed = 1;
        unsigned int snaps = 0;
        double last_drift = 0;

        void reset(double position, double now);
        void set_speed(double speed, double now);
        void sync(double reported_position, double now);
        double position_at(double now) const;
    };
}
