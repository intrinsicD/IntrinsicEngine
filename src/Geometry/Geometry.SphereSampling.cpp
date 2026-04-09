module;

#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <random>

module Geometry.SphereSampling;

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

        inline double SquaredNorm(const glm::vec3& v)
        {
            return glm::dot(v, v);
        }
    }

    std::vector<glm::vec3> SampleSurfaceRandom(const Sphere& sphere, size_t num_samples)
    {
        std::vector<glm::vec3> points(num_samples);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<double> dist(-1.0, 1.0);

        // Generate random points inside the unit sphere and then scale them to the sphere's radius
        // Ensure the points are uniformly distributed over the sphere's surface
        for (size_t i = 0; i < num_samples; ++i)
        {
            glm::vec3 point;
            do
            {
                point = {dist(gen), dist(gen), dist(gen)};
            }
            while (helper::SquaredNorm(point) > 1.0); // Ensure the point is inside the unit sphere
            points[i] = glm::normalize(point) * sphere.Radius + sphere.Center;
        }
        return points;
    }


    std::vector<glm::vec3> SampleSurfaceUniform(const Sphere& sphere, size_t num_samples)
    {
        std::vector<glm::vec3> points(num_samples);
        double phi = M_PI * (3.0 - std::sqrt(5.0)); // Golden angle in radians
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

    std::vector<glm::vec3> SampleSurfaceFibonacciLattice(const Sphere& sphere, size_t num_samples,
                                                         FibonacciLattice type = FLTHIRD)
    {
        //http://extremelearning.com.au/evenly-distributing-points-on-a-sphere/
        std::vector<glm::vec3> points(num_samples);

        double golden_ratio = (1.0 + std::sqrt(5.0)) / 2.0;
        double TWOPI = 2 * M_PI;
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

    std::vector<glm::vec3> SampleVolumeRandom(const Sphere& sphere, size_t num_samples)
    {
        std::vector<glm::vec3> points(num_samples);

        // 1) random_device + mt19937 for high‐quality randomness
        std::random_device rd;
        std::mt19937 gen(rd());

        // 2) Generate uniform real in [-1,1] (to be scaled by sphere.Radius)
        std::uniform_real_distribution<double> dist(-1.0, 1.0);

        // 3) Rejection sampling in the cube [−R,R]^3:
        //    - Draw (x,y,z) in [−1,1]^3, scale by R
        //    - Reject if x^2+y^2+z^2 > R^2
        //    This yields a uniform distribution over the sphere’s volume :contentReference[oaicite:0]{index=0}
        for (size_t i = 0; i < num_samples; ++i)
        {
            glm::vec3 point;
            do
            {
                // a) random sample in unit cube
                point = {dist(gen), dist(gen), dist(gen)};
                // b) scale to actual sphere radius
                point *= sphere.Radius;
                // c) check if inside sphere: squaredNorm() <= R^2
            }
            while (helper::SquaredNorm(point) > (sphere.Radius * sphere.Radius));

            // 4) Translate to sphere center
            points[i] = point + sphere.Center;
        }

        return points;
    }

    //Aaron R. Voelker, Jan Gosmann, Terrence C. Stewart. “Efficiently sampling vectors and coordinates from the n‐sphere and n‐ball,” Centre for Theoretical Neuroscience, University of Waterloo (2017).
    //“Uniformly at random within the n‐ball,” Wikipedia (accessed May 2025).
    //“Walk-on-spheres method,” Wikipedia (accessed May 2025).

    std::vector<glm::vec3> SampleVolumeUniform(const Sphere& sphere, size_t num_samples)
    {
        std::vector<glm::vec3> points;
        points.reserve(num_samples);

        // 1) Use std::random_device + mt19937 for high‐quality randomness
        std::random_device rd;
        std::mt19937 gen(rd());

        // 2) We'll need three independent Uniform[0,1] draws (u, u', u''):
        std::uniform_real_distribution<double> dist01(0.0, 1.0);

        for (size_t i = 0; i < num_samples; ++i)
        {
            // a) Draw u ∼ Uniform[0,1] for radial coordinate:
            //    r = sphere.Radius * ∛u ensures uniformity in volume (Jacobian ∝ r^2).
            //    :contentReference[oaicite:0]{index=0}
            double u = dist01(gen);
            double r = sphere.Radius * std::cbrt(u);

            // b) Draw u' ∼ Uniform[0,1] to get cosθ ∈ [−1,1]:
            //    cosθ = 1 − 2u' ⇒ θ has pdf ∝ sinθ, giving uniform distribution on the sphere’s surface :contentReference[oaicite:1]{index=1}
            double u_prime = dist01(gen);
            double cos_theta = 1.0 - 2.0 * u_prime;
            double sin_theta = std::sqrt(std::max<double>(0, 1.0 - cos_theta * cos_theta));
            // guard against tiny negatives

            // c) Draw u'' ∼ Uniform[0,1] to get φ ∈ [0,2π):
            //    φ = 2π·u''  gives uniform azimuthal angle. :contentReference[oaicite:2]{index=2}
            double u_doubleprime = dist01(gen);
            double phi = 2.0 * M_PI * u_doubleprime;

            // d) Convert (r, θ, φ) → Cartesian unit‐vector * r:
            //
            //    x = r · sinθ · cosφ
            //    y = r · sinθ · sinφ
            //    z = r · cosθ
            glm::vec3 dir = {
                sin_theta * std::cos(phi),
                sin_theta * std::sin(phi),
                cos_theta
            };

            // e) Scale by r and translate by sphere.Center:
            points.push_back(float(r) * dir + sphere.Center);
        }

        return points;
    }
}
