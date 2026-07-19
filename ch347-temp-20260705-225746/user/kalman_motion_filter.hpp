#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>

namespace motion_filter {

struct Vec2 {
    double X;
    double Y;
};

inline Vec2 operator+(Vec2 a, Vec2 b)
{
    return {a.X + b.X, a.Y + b.Y};
}

inline Vec2 operator-(Vec2 a, Vec2 b)
{
    return {a.X - b.X, a.Y - b.Y};
}

inline Vec2 operator*(Vec2 v, double scale)
{
    return {v.X * scale, v.Y * scale};
}

inline double Length(Vec2 v)
{
    return std::sqrt(v.X * v.X + v.Y * v.Y);
}

inline Vec2 NormalizeOrZero(Vec2 v)
{
    const double length = Length(v);
    if (length <= 1e-9) {
        return {0.0, 0.0};
    }

    return {v.X / length, v.Y / length};
}

inline double Clamp(double value, double minValue, double maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

inline double SmoothStep01(double value)
{
    const double t = Clamp(value, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

class Kalman1D {
public:
    Kalman1D()
        : position_(0.0),
          velocity_(0.0),
          p00_(1.0),
          p01_(0.0),
          p10_(0.0),
          p11_(1.0),
          processNoise_(25.0),
          measurementNoise_(9.0),
          initialized_(false)
    {
    }

    void Configure(double processNoise, double measurementNoise)
    {
        processNoise_ = std::max(1e-9, processNoise);
        measurementNoise_ = std::max(1e-9, measurementNoise);
    }

    void Reset(double position, double velocity = 0.0)
    {
        position_ = position;
        velocity_ = velocity;
        p00_ = 1.0;
        p01_ = 0.0;
        p10_ = 0.0;
        p11_ = 1.0;
        initialized_ = true;
    }

    double Update(double measurement, double dtSeconds)
    {
        if (!initialized_) {
            Reset(measurement);
            return position_;
        }

        const double dt = std::max(1e-6, dtSeconds);
        Predict(dt);
        Correct(measurement);
        return position_;
    }

    double PredictOnly(double dtSeconds)
    {
        if (!initialized_) {
            return position_;
        }

        Predict(std::max(1e-6, dtSeconds));
        return position_;
    }

    bool Initialized() const
    {
        return initialized_;
    }

    double Position() const
    {
        return position_;
    }

    double Velocity() const
    {
        return velocity_;
    }

private:
    void Predict(double dt)
    {
        const double dt2 = dt * dt;
        const double dt3 = dt2 * dt;
        const double dt4 = dt2 * dt2;

        position_ += velocity_ * dt;

        const double q00 = processNoise_ * dt4 * 0.25;
        const double q01 = processNoise_ * dt3 * 0.5;
        const double q10 = q01;
        const double q11 = processNoise_ * dt2;

        const double oldP00 = p00_;
        const double oldP01 = p01_;
        const double oldP10 = p10_;
        const double oldP11 = p11_;

        p00_ = oldP00 + dt * (oldP10 + oldP01) + dt2 * oldP11 + q00;
        p01_ = oldP01 + dt * oldP11 + q01;
        p10_ = oldP10 + dt * oldP11 + q10;
        p11_ = oldP11 + q11;
    }

    void Correct(double measurement)
    {
        const double innovation = measurement - position_;
        const double innovationCov = p00_ + measurementNoise_;
        const double k0 = p00_ / innovationCov;
        const double k1 = p10_ / innovationCov;

        const double oldP00 = p00_;
        const double oldP01 = p01_;
        const double oldP10 = p10_;
        const double oldP11 = p11_;

        position_ += k0 * innovation;
        velocity_ += k1 * innovation;

        p00_ = (1.0 - k0) * oldP00;
        p01_ = (1.0 - k0) * oldP01;
        p10_ = oldP10 - k1 * oldP00;
        p11_ = oldP11 - k1 * oldP01;
    }

    double position_;
    double velocity_;
    double p00_;
    double p01_;
    double p10_;
    double p11_;
    double processNoise_;
    double measurementNoise_;
    bool initialized_;
};

class Kalman2D {
public:
    void Configure(double processNoise, double measurementNoise)
    {
        x_.Configure(processNoise, measurementNoise);
        y_.Configure(processNoise, measurementNoise);
    }

    void Reset(Vec2 position, Vec2 velocity = {0.0, 0.0})
    {
        x_.Reset(position.X, velocity.X);
        y_.Reset(position.Y, velocity.Y);
    }

    Vec2 Update(Vec2 measurement, double dtSeconds)
    {
        return {
            x_.Update(measurement.X, dtSeconds),
            y_.Update(measurement.Y, dtSeconds)
        };
    }

    Vec2 PredictOnly(double dtSeconds)
    {
        return {
            x_.PredictOnly(dtSeconds),
            y_.PredictOnly(dtSeconds)
        };
    }

    bool Initialized() const
    {
        return x_.Initialized() && y_.Initialized();
    }

    Vec2 Position() const
    {
        return {x_.Position(), y_.Position()};
    }

    Vec2 Velocity() const
    {
        return {x_.Velocity(), y_.Velocity()};
    }

private:
    Kalman1D x_;
    Kalman1D y_;
};

struct MotionProfileConfig {
    double MaxSpeed = 1800.0;
    double MaxAcceleration = 12000.0;
    double SlowRadius = 180.0;
    double DeadZone = 0.25;
};

struct MotionProfileState {
    Vec2 Position = {0.0, 0.0};
    Vec2 Velocity = {0.0, 0.0};
};

struct McuMovePacket8 {
    std::array<std::uint8_t, 8> Bytes{};
};

enum class McuChecksumMode {
    Sum8,
    Xor8,
    TwosComplementSum8
};

struct McuPacketizerConfig {
    std::uint16_t Header = 0xA55A;
    std::uint8_t CommandMove = 0x01;
    McuChecksumMode ChecksumMode = McuChecksumMode::Sum8;
    double CountsPerUnit = 1.0;
    bool InvertX = false;
    bool InvertY = false;
};

struct McuPacketizerState {
    double ResidualX = 0.0;
    double ResidualY = 0.0;

    void Reset()
    {
        ResidualX = 0.0;
        ResidualY = 0.0;
    }
};

struct McuDecodedMove {
    std::uint16_t Header = 0;
    std::uint8_t Command = 0;
    std::int16_t Dx = 0;
    std::int16_t Dy = 0;
    std::uint8_t Checksum = 0;
    bool HeaderOk = false;
    bool CommandOk = false;
    bool ChecksumOk = false;

    bool Valid() const
    {
        return HeaderOk && CommandOk && ChecksumOk;
    }
};

struct MotionPipelineConfig {
    double ProcessNoise = 35.0;
    double MeasurementNoise = 6.0;
    bool EnableOutlierGate = false;
    double MaxMeasurementJump = 250.0;
    MotionProfileConfig Motion;
    McuPacketizerConfig Packet;
};

struct MotionPipelineOutput {
    Vec2 Raw = {0.0, 0.0};
    Vec2 Filtered = {0.0, 0.0};
    Vec2 MotionPosition = {0.0, 0.0};
    Vec2 MotionVelocity = {0.0, 0.0};
    Vec2 Delta = {0.0, 0.0};
    McuMovePacket8 Packet;
    McuDecodedMove Decoded;
    bool MeasurementAccepted = false;
    bool Initialized = false;
};

inline Vec2 LimitDelta(Vec2 current, Vec2 desired, double maxDelta)
{
    const Vec2 delta = desired - current;
    const double deltaLength = Length(delta);
    if (deltaLength <= maxDelta || deltaLength <= 1e-9) {
        return desired;
    }

    return current + NormalizeOrZero(delta) * maxDelta;
}

inline MotionProfileState StepDistanceScaledMotion(
    MotionProfileState state,
    Vec2 target,
    double dtSeconds,
    const MotionProfileConfig& config)
{
    const double dt = std::max(1e-6, dtSeconds);
    const double maxSpeed = std::max(0.0, config.MaxSpeed);
    const double maxAcceleration = std::max(0.0, config.MaxAcceleration);
    const double deadZone = std::max(0.0, config.DeadZone);
    const Vec2 toTarget = target - state.Position;
    const double distance = Length(toTarget);

    if (distance <= deadZone) {
        state.Position = target;
        state.Velocity = {0.0, 0.0};
        return state;
    }

    const Vec2 direction = NormalizeOrZero(toTarget);
    const double radius = std::max(deadZone, config.SlowRadius);
    const double speedScale = SmoothStep01(distance / radius);
    const double desiredSpeed = Clamp(
        maxSpeed * speedScale,
        0.0,
        maxSpeed);
    const Vec2 desiredVelocity = direction * desiredSpeed;

    const double maxVelocityDelta = maxAcceleration * dt;
    state.Velocity = LimitDelta(state.Velocity, desiredVelocity, maxVelocityDelta);

    Vec2 nextPosition = state.Position + state.Velocity * dt;
    const Vec2 remainingAfterStep = target - nextPosition;
    if ((toTarget.X * remainingAfterStep.X + toTarget.Y * remainingAfterStep.Y) <= 0.0) {
        nextPosition = target;
        state.Velocity = {0.0, 0.0};
    }

    state.Position = nextPosition;
    return state;
}

inline std::int16_t SaturateToInt16(double value)
{
    if (!std::isfinite(value)) {
        return 0;
    }

    const double rounded = std::round(value);
    const double minValue = static_cast<double>(std::numeric_limits<std::int16_t>::min());
    const double maxValue = static_cast<double>(std::numeric_limits<std::int16_t>::max());

    return static_cast<std::int16_t>(Clamp(rounded, minValue, maxValue));
}

inline void StoreLe16(std::array<std::uint8_t, 8>& bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
}

inline std::uint16_t LoadLe16(const std::array<std::uint8_t, 8>& bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
}

inline std::uint8_t ComputeChecksum8(
    const std::array<std::uint8_t, 8>& bytes,
    McuChecksumMode mode)
{
    std::uint32_t sum = 0;
    std::uint8_t xorValue = 0;

    for (std::size_t i = 0; i < 7; ++i) {
        sum += bytes[i];
        xorValue = static_cast<std::uint8_t>(xorValue ^ bytes[i]);
    }

    switch (mode) {
    case McuChecksumMode::Xor8:
        return xorValue;
    case McuChecksumMode::TwosComplementSum8:
        return static_cast<std::uint8_t>((0u - sum) & 0xFFu);
    case McuChecksumMode::Sum8:
    default:
        return static_cast<std::uint8_t>(sum & 0xFFu);
    }
}

inline std::uint8_t ChecksumSum8(const std::array<std::uint8_t, 8>& bytes)
{
    return ComputeChecksum8(bytes, McuChecksumMode::Sum8);
}

inline McuMovePacket8 PacketizeMoveCounts(
    std::int16_t dx,
    std::int16_t dy,
    const McuPacketizerConfig& config = {})
{
    McuMovePacket8 packet = {};
    const std::uint16_t encodedDx = static_cast<std::uint16_t>(dx);
    const std::uint16_t encodedDy = static_cast<std::uint16_t>(dy);

    StoreLe16(packet.Bytes, 0, config.Header);
    packet.Bytes[2] = config.CommandMove;
    StoreLe16(packet.Bytes, 3, encodedDx);
    StoreLe16(packet.Bytes, 5, encodedDy);
    packet.Bytes[7] = ComputeChecksum8(packet.Bytes, config.ChecksumMode);

    return packet;
}

inline McuMovePacket8 PacketizeMoveDelta(
    Vec2 delta,
    McuPacketizerState& state,
    const McuPacketizerConfig& config = {})
{
    const double scale = std::max(1e-9, config.CountsPerUnit);
    const double signedX = config.InvertX ? -delta.X : delta.X;
    const double signedY = config.InvertY ? -delta.Y : delta.Y;
    const double adjustedX = signedX * scale + state.ResidualX;
    const double adjustedY = signedY * scale + state.ResidualY;

    const std::int16_t dx = SaturateToInt16(adjustedX);
    const std::int16_t dy = SaturateToInt16(adjustedY);

    state.ResidualX = adjustedX - static_cast<double>(dx);
    state.ResidualY = adjustedY - static_cast<double>(dy);

    if (dx == std::numeric_limits<std::int16_t>::min() ||
        dx == std::numeric_limits<std::int16_t>::max()) {
        state.ResidualX = 0.0;
    }

    if (dy == std::numeric_limits<std::int16_t>::min() ||
        dy == std::numeric_limits<std::int16_t>::max()) {
        state.ResidualY = 0.0;
    }

    return PacketizeMoveCounts(dx, dy, config);
}

inline McuDecodedMove DecodeMovePacket(
    const McuMovePacket8& packet,
    const McuPacketizerConfig& config = {})
{
    McuDecodedMove decoded;
    decoded.Header = LoadLe16(packet.Bytes, 0);
    decoded.Command = packet.Bytes[2];
    decoded.Dx = static_cast<std::int16_t>(LoadLe16(packet.Bytes, 3));
    decoded.Dy = static_cast<std::int16_t>(LoadLe16(packet.Bytes, 5));
    decoded.Checksum = packet.Bytes[7];
    decoded.HeaderOk = (decoded.Header == config.Header);
    decoded.CommandOk = (decoded.Command == config.CommandMove);
    decoded.ChecksumOk = (decoded.Checksum == ComputeChecksum8(packet.Bytes, config.ChecksumMode));
    return decoded;
}

class MotionPacketPipeline {
public:
    explicit MotionPacketPipeline(MotionPipelineConfig config = {})
        : config_(config),
          initialized_(false)
    {
        filter_.Configure(config_.ProcessNoise, config_.MeasurementNoise);
    }

    void Configure(MotionPipelineConfig config)
    {
        config_ = config;
        filter_.Configure(config_.ProcessNoise, config_.MeasurementNoise);
    }

    void Reset(Vec2 initialPosition, Vec2 initialVelocity = {0.0, 0.0})
    {
        filter_.Configure(config_.ProcessNoise, config_.MeasurementNoise);
        filter_.Reset(initialPosition, initialVelocity);
        motion_.Position = initialPosition;
        motion_.Velocity = initialVelocity;
        packetizer_.Reset();
        initialized_ = true;
    }

    MotionPipelineOutput Step(Vec2 measurement, double dtSeconds, bool measurementValid = true)
    {
        if (!initialized_) {
            if (measurementValid) {
                Reset(measurement);
            } else {
                return MakeIdleOutput(measurement);
            }
        }

        const bool accepted = measurementValid && AcceptMeasurement(measurement);
        const Vec2 filtered = accepted
            ? filter_.Update(measurement, dtSeconds)
            : filter_.PredictOnly(dtSeconds);

        const MotionProfileState previousMotion = motion_;
        motion_ = StepDistanceScaledMotion(motion_, filtered, dtSeconds, config_.Motion);

        const Vec2 delta = motion_.Position - previousMotion.Position;
        const McuMovePacket8 packet = PacketizeMoveDelta(delta, packetizer_, config_.Packet);

        MotionPipelineOutput output;
        output.Raw = measurement;
        output.Filtered = filtered;
        output.MotionPosition = motion_.Position;
        output.MotionVelocity = motion_.Velocity;
        output.Delta = delta;
        output.Packet = packet;
        output.Decoded = DecodeMovePacket(packet, config_.Packet);
        output.MeasurementAccepted = accepted;
        output.Initialized = initialized_;

        return output;
    }

    MotionPipelineOutput Predict(double dtSeconds)
    {
        return Step(filter_.Position(), dtSeconds, false);
    }

    const MotionPipelineConfig& Config() const
    {
        return config_;
    }

private:
    bool AcceptMeasurement(Vec2 measurement) const
    {
        if (!config_.EnableOutlierGate) {
            return true;
        }

        const Vec2 delta = measurement - filter_.Position();
        return Length(delta) <= std::max(0.0, config_.MaxMeasurementJump);
    }

    MotionPipelineOutput MakeIdleOutput(Vec2 raw) const
    {
        const McuMovePacket8 packet = PacketizeMoveCounts(0, 0, config_.Packet);

        MotionPipelineOutput output;
        output.Raw = raw;
        output.Packet = packet;
        output.Decoded = DecodeMovePacket(packet, config_.Packet);
        return output;
    }

    MotionPipelineConfig config_;
    Kalman2D filter_;
    MotionProfileState motion_;
    McuPacketizerState packetizer_;
    bool initialized_;
};

} // namespace motion_filter
