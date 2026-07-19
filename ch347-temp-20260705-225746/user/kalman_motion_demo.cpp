#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

#include "kalman_motion_filter.hpp"

static std::string PacketHex(const motion_filter::McuMovePacket8& packet)
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');

    for (std::size_t i = 0; i < packet.Bytes.size(); ++i) {
        if (i != 0) {
            stream << ' ';
        }

        stream << std::setw(2) << static_cast<unsigned int>(packet.Bytes[i]);
    }

    return stream.str();
}

static bool RunSelfTest()
{
    using motion_filter::DecodeMovePacket;
    using motion_filter::McuChecksumMode;
    using motion_filter::McuPacketizerConfig;
    using motion_filter::McuPacketizerState;
    using motion_filter::PacketizeMoveCounts;
    using motion_filter::PacketizeMoveDelta;
    using motion_filter::Vec2;

    McuPacketizerConfig config;
    config.Header = 0xA55A;
    config.CommandMove = 0x01;

    const auto packet = PacketizeMoveCounts(1, -2, config);
    const std::array<std::uint8_t, 8> expected = {
        0x5A, 0xA5, 0x01, 0x01, 0x00, 0xFE, 0xFF, 0xFE
    };

    if (packet.Bytes != expected) {
        std::cerr << "sum8 packet mismatch: " << PacketHex(packet) << '\n';
        return false;
    }

    const auto decoded = DecodeMovePacket(packet, config);
    if (!decoded.Valid() || decoded.Dx != 1 || decoded.Dy != -2) {
        std::cerr << "decode validation failed\n";
        return false;
    }

    config.ChecksumMode = McuChecksumMode::Xor8;
    const auto xorPacket = PacketizeMoveCounts(1, -2, config);
    if (!DecodeMovePacket(xorPacket, config).Valid()) {
        std::cerr << "xor checksum validation failed\n";
        return false;
    }

    config.ChecksumMode = McuChecksumMode::Sum8;
    McuPacketizerState residual;
    const auto p0 = PacketizeMoveDelta({0.4, 0.0}, residual, config);
    const auto p1 = PacketizeMoveDelta({0.4, 0.0}, residual, config);

    if (DecodeMovePacket(p0, config).Dx != 0 ||
        DecodeMovePacket(p1, config).Dx != 1) {
        std::cerr << "fractional residual validation failed\n";
        return false;
    }

    std::cout << "self-test ok\n";
    return true;
}

int main(int argc, char **argv)
{
    using motion_filter::MotionPacketPipeline;
    using motion_filter::MotionPipelineConfig;
    using motion_filter::Vec2;

    if (argc == 2 && std::string(argv[1]) == "--self-test") {
        return RunSelfTest() ? 0 : 1;
    }

    constexpr double dt = 1.0 / 120.0;

    const Vec2 noisyMeasurements[] = {
        {100.0, 80.0},
        {101.8, 79.2},
        {103.1, 81.0},
        {105.2, 80.5},
        {107.9, 82.4},
        {109.0, 81.7},
        {112.5, 83.1},
        {115.0, 84.2},
        {116.2, 83.6},
        {119.7, 85.0},
    };

    MotionPipelineConfig pipelineConfig;
    pipelineConfig.ProcessNoise = 35.0;
    pipelineConfig.MeasurementNoise = 6.0;
    pipelineConfig.EnableOutlierGate = true;
    pipelineConfig.MaxMeasurementJump = 80.0;
    pipelineConfig.Motion.MaxSpeed = 900.0;
    pipelineConfig.Motion.MaxAcceleration = 6000.0;
    pipelineConfig.Motion.SlowRadius = 120.0;
    pipelineConfig.Motion.DeadZone = 0.2;
    pipelineConfig.Packet.Header = 0xA55A;
    pipelineConfig.Packet.CommandMove = 0x01;
    pipelineConfig.Packet.CountsPerUnit = 1.0;

    MotionPacketPipeline pipeline(pipelineConfig);

    std::cout
        << "frame,raw_x,raw_y,kalman_x,kalman_y,motion_x,motion_y,vel_x,vel_y,delta_x,delta_y,accepted,packet_valid,packet8\n"
        << std::fixed << std::setprecision(3);

    for (int frame = 0; frame < static_cast<int>(std::size(noisyMeasurements)); ++frame) {
        const Vec2 raw = noisyMeasurements[frame];
        const auto output = pipeline.Step(raw, dt);

        std::cout
            << frame << ','
            << raw.X << ',' << raw.Y << ','
            << output.Filtered.X << ',' << output.Filtered.Y << ','
            << output.MotionPosition.X << ',' << output.MotionPosition.Y << ','
            << output.MotionVelocity.X << ',' << output.MotionVelocity.Y << ','
            << output.Delta.X << ',' << output.Delta.Y << ','
            << (output.MeasurementAccepted ? 1 : 0) << ','
            << (output.Decoded.Valid() ? 1 : 0) << ','
            << '"' << PacketHex(output.Packet) << '"' << '\n';
    }

    return 0;
}
