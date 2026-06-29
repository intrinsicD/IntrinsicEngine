module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <random>
#include <span>
#include <vector>

#include <glm/glm.hpp>

module Geometry.Sphere.Sampling;

import Geometry.Sampling;
import Geometry.Sphere;

namespace Geometry::Sampling
{
    namespace helper
    {
        inline glm::vec3 LatticePoint(size_t i, size_t num_samples, double golden_ratio, double TWOPI,
                                      double index_offset,
                                      double sample_count_offset, const Sphere& sphere)
        {
            double x = (i + index_offset) / double(num_samples + sample_count_offset);
            double y = double(i) / golden_ratio;
            double phi = std::acos(1.0 - 2.0 * x);
            double theta = TWOPI * y;
            glm::vec3 point = {
                std::cos(theta) * std::sin(phi), std::sin(theta) * std::sin(phi),
                std::cos(phi)
            };
            return point * sphere.Radius + sphere.Center;
        }

        [[nodiscard]] std::mt19937_64 MakeDeterministicGenerator(std::uint64_t seed)
        {
            return std::mt19937_64(MixSeed(seed));
        }
    }

    std::vector<glm::vec3> SampleSurfaceRandom(const Sphere& sphere, std::uint32_t num_samples)
    {
        return SampleSurfaceRandom(sphere, num_samples, kDefaultSphereSamplingSeed);
    }

    std::vector<glm::vec3> SampleSurfaceRandom(const Sphere& sphere, std::uint32_t num_samples, std::uint64_t seed)
    {
        std::vector<glm::vec3> points(num_samples);
        std::mt19937_64 rng = helper::MakeDeterministicGenerator(seed);
        std::uniform_real_distribution<double> unit(0.0, 1.0);

        for (size_t i = 0; i < num_samples; ++i)
        {
            const double zSample = unit(rng);
            const double azimuthSample = unit(rng);
            const glm::vec3 direction = UniformUnitVectorFromUnitSamples(zSample, azimuthSample);
            points[i] = direction * sphere.Radius + sphere.Center;
        }
        return points;
    }


    std::vector<glm::vec3> SampleSurfaceUniform(const Sphere& sphere, size_t num_samples)
    {
        std::vector<glm::vec3> points(num_samples);
        double phi = std::numbers::pi_v<double> * (3.0 - std::sqrt(5.0)); // Golden angle in radians
        double radius = sphere.Radius;

        for (size_t i = 0; i < num_samples; ++i)
        {
            double y = 1 - (i / double(num_samples - 1)) * 2; // y goes from 1 to -1
            double radius_at_y = std::sqrt(1 - y * y); // Radius at this y level
            double theta = phi * i; // Golden angle increment

            points[i] = {
                sphere.Center.x + radius_at_y * std::cos(theta) * radius,
                sphere.Center.y + radius_at_y * std::sin(theta) * radius,
                sphere.Center.z + y * radius
            };
        }
        return points;
    }

    //http://extremelearning.com.au/evenly-distributing-points-on-a-sphere/
    //Thou et. al. - 2024 - https://arxiv.org/pdf/2410.12007v1
    //Alvaro Gonzalez - 2009 - https://arxiv.org/pdf/0912.4540

    std::vector<glm::vec3> SampleSurfaceFibonacciLattice(const Sphere& sphere, std::uint32_t num_samples,
                                                         FibonacciLattice type = FLTHIRD)
    {
        //http://extremelearning.com.au/evenly-distributing-points-on-a-sphere/
        std::vector<glm::vec3> points(num_samples);

        double golden_ratio = (1.0 + std::sqrt(5.0)) / 2.0;
        double TWOPI = 2 * std::numbers::pi_v<double>;
        double epsilon = 0.36;
        size_t start_index = 0;
        size_t end_index = num_samples;
        double offset = 0.0;
        double sample_count_offset = 0.0;
        switch (type)
        {
        default:
        case FLNAIVE:
            {
                offset = 0.0;
                sample_count_offset = 0.0;
                break;
            }
        case FLFIRST:
            {
                offset = 0.5;
                sample_count_offset = 0.0;
                break;
            }
        case FLSECOND:
            {
                offset = 1.5;
                sample_count_offset = 2 * offset;
                break;
            }
        case FLTHIRD:
            {
                offset = 3.5;
                sample_count_offset = 2 * offset;
                start_index = 1;
                end_index = num_samples - 1;
                break;
            }
        case FLOFFSET:
            {
                offset = epsilon;
                sample_count_offset = 2 * offset - 1;
                start_index = 1;
                end_index = num_samples - 1;
                break;
            }
        }

        if (type == FLTHIRD || type == FLOFFSET)
        {
            points[start_index] = glm::vec3(0, 0, 1) * sphere.Radius + sphere.Center;
        }
        for (size_t i = start_index; i < end_index; ++i)
        {
            points[i] = helper::LatticePoint(i, num_samples, golden_ratio, TWOPI, offset, sample_count_offset, sphere);
        }
        if (type == FLTHIRD || type == FLOFFSET)
        {
            points[end_index] = glm::vec3(0, 0, -1) * sphere.Radius + sphere.Center;
        }
        return points;
    }

    //Aaron R. Voelker, Jan Gosmann, Terrence C. Stewart. “Efficiently sampling vectors and coordinates from the n‐sphere and n‐ball,” Centre for Theoretical Neuroscience, University of Waterloo (2017).
    //“Uniformly at random within the n‐ball,” Wikipedia (accessed May 2025).

    std::vector<glm::vec3> SampleVolumeRandom(const Sphere& sphere, std::uint32_t num_samples)
    {
        return SampleVolumeRandom(sphere, num_samples, kDefaultSphereSamplingSeed);
    }

    std::vector<glm::vec3> SampleVolumeRandom(const Sphere& sphere, std::uint32_t num_samples, std::uint64_t seed)
    {
        std::vector<glm::vec3> points(num_samples);
        std::mt19937_64 rng = helper::MakeDeterministicGenerator(seed);
        std::uniform_real_distribution<double> unit(0.0, 1.0);

        for (size_t i = 0; i < num_samples; ++i)
        {
            const double radiusSample = unit(rng);
            const double zSample = unit(rng);
            const double azimuthSample = unit(rng);
            points[i] = UniformUnitBallPointFromUnitSamples(radiusSample, zSample, azimuthSample)
                * sphere.Radius
                + sphere.Center;
        }

        return points;
    }

    //Aaron R. Voelker, Jan Gosmann, Terrence C. Stewart. “Efficiently sampling vectors and coordinates from the n‐sphere and n‐ball,” Centre for Theoretical Neuroscience, University of Waterloo (2017).
    //“Uniformly at random within the n‐ball,” Wikipedia (accessed May 2025).
    //“Walk-on-spheres method,” Wikipedia (accessed May 2025).

    std::vector<glm::vec3> SampleVolumeUniform(const Sphere& sphere, std::uint32_t num_samples)
    {
        return SampleVolumeUniform(sphere, num_samples, kDefaultSphereSamplingSeed);
    }

    std::vector<glm::vec3> SampleVolumeUniform(const Sphere& sphere, std::uint32_t num_samples, std::uint64_t seed)
    {
        std::vector<glm::vec3> points;
        points.reserve(num_samples);
        std::mt19937_64 rng = helper::MakeDeterministicGenerator(seed);
        std::uniform_real_distribution<double> unit(0.0, 1.0);

        for (size_t i = 0; i < num_samples; ++i)
        {
            const double radiusSample = unit(rng);
            const double zSample = unit(rng);
            const double azimuthSample = unit(rng);
            const glm::vec3 point = UniformUnitBallPointFromUnitSamples(radiusSample, zSample, azimuthSample)
                * sphere.Radius
                + sphere.Center;
            points.push_back(point);
        }

        return points;
    }
}
