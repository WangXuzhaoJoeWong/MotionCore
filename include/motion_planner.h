#pragma once

#include <memory>
#include <string>
#include <vector>

namespace wxz::core {

struct Pose {
    std::vector<double> joints;
};

struct TrajectoryPoint {
    std::vector<double> joints;
    double time_from_start{0.0};
};

struct Trajectory {
    std::vector<TrajectoryPoint> points;
};

class ICollisionChecker {
public:
    virtual ~ICollisionChecker() = default;
    virtual bool isCollisionFree(const Trajectory& traj) const = 0;
};

class IMotionPlanner {
public:
    virtual ~IMotionPlanner() = default;
    virtual std::string plannerName() const = 0;
    virtual bool plan(const Pose& start, const Pose& goal, Trajectory& result) = 0;
    virtual void setCollisionChecker(std::shared_ptr<ICollisionChecker> checker) = 0;
};

} // namespace wxz::core
