//==============================================================================================
// Originally written in 2016 by Peter Shirley <ptrshrl@gmail.com>
//
// To the extent possible under law, the author(s) have dedicated all copyright and related and
// neighboring rights to this software to the public domain worldwide. This software is
// distributed without any warranty.
//
// You should have received a copy (see file COPYING.txt) of the CC0 Public Domain Dedication
// along with this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
//==============================================================================================

#include "rtweekend.h"

#include "camera.h"
#include "color.h"
#include "hittable_list.h"
#include "material.h"
#include "sphere.h"

#include <iostream>

#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>


const auto aspect_ratio = 16.0 / 9.0;
const int image_width = 1200;
const int image_height = static_cast<int>(image_width / aspect_ratio);
const int samples_per_pixel = 10;
const int max_depth = 50;


color ray_color(const ray& r, const hittable& world, int depth) {
    hit_record rec;

    // If we've exceeded the ray bounce limit, no more light is gathered.
    if (depth <= 0)
        return color(0,0,0);

    if (world.hit(r, 0.001, infinity, rec)) {
        ray scattered;
        color attenuation;
        if (rec.mat_ptr->scatter(r, rec, attenuation, scattered))
            return attenuation * ray_color(scattered, world, depth-1);
        return color(0,0,0);
    }

    vec3 unit_direction = unit_vector(r.direction());
    auto t = 0.5*(unit_direction.y() + 1.0);
    return (1.0-t)*color(1.0, 1.0, 1.0) + t*color(0.5, 0.7, 1.0);
}


hittable_list random_scene() {
    hittable_list world;

    auto ground_material = make_shared<lambertian>(color(0.5, 0.5, 0.5));
    world.add(make_shared<sphere>(point3(0,-1000,0), 1000, ground_material));

    for (int a = -11; a < 11; a++) {
        for (int b = -11; b < 11; b++) {
            auto choose_mat = random_double();
            point3 center(a + 0.9*random_double(), 0.2, b + 0.9*random_double());

            if ((center - point3(4, 0.2, 0)).length() > 0.9) {
                shared_ptr<material> sphere_material;

                if (choose_mat < 0.8) {
                    // diffuse
                    auto albedo = color::random() * color::random();
                    sphere_material = make_shared<lambertian>(albedo);
                    world.add(make_shared<sphere>(center, 0.2, sphere_material));
                } else if (choose_mat < 0.95) {
                    // metal
                    auto albedo = color::random(0.5, 1);
                    auto fuzz = random_double(0, 0.5);
                    sphere_material = make_shared<metal>(albedo, fuzz);
                    world.add(make_shared<sphere>(center, 0.2, sphere_material));
                } else {
                    // glass
                    sphere_material = make_shared<dielectric>(1.5);
                    world.add(make_shared<sphere>(center, 0.2, sphere_material));
                }
            }
        }
    }

    auto material1 = make_shared<dielectric>(1.5);
    world.add(make_shared<sphere>(point3(0, 1, 0), 1.0, material1));

    auto material2 = make_shared<lambertian>(color(0.4, 0.2, 0.1));
    world.add(make_shared<sphere>(point3(-4, 1, 0), 1.0, material2));

    auto material3 = make_shared<metal>(color(0.7, 0.6, 0.5), 0.0);
    world.add(make_shared<sphere>(point3(4, 1, 0), 1.0, material3));

    return world;
}


void render_line(camera cam, const hittable_list world, color *rendered_image, const int line_number) {
    for (int i = 0; i < image_width; ++i) {
        color pixel_color(0,0,0);
        for (int s = 0; s < samples_per_pixel; ++s) {
            auto u = (i + random_double()) / (image_width-1);
            auto v = (line_number + random_double()) / (image_height-1);
            ray r = cam.get_ray(u, v);
            pixel_color += ray_color(r, world, max_depth);
        }
        rendered_image[line_number * image_width + i] = pixel_color;
    }
}


void render_image(camera cam, const hittable_list world, color *rendered_image, int number_of_threads) {
    int lines_per_child = image_height / number_of_threads;
    int rest = image_height % number_of_threads;

    for (int i = 0; i < number_of_threads; ++i) {
        pid_t pid = fork(); 
        if (pid == 0) { // child
            for (int l = 0; l < lines_per_child; ++l) {
                render_line(cam, world, rendered_image, (i * lines_per_child + l));
            }
            exit(0);
        } else if (rest != 0) { // render rest in parrent
            for (int l = 0; l < rest; ++l) {
                render_line(cam, world, rendered_image, ((number_of_threads) * lines_per_child + l));
            }
        } else if (pid < 0) {
            std::cerr << "fork failed";
            exit(1);
        }
    }

    for (int w = 0; w < number_of_threads; w++) {
        pid_t pid = wait(NULL);
        if (pid < 0) {
            std::cerr << "wait failed";
        }
    }
}


int main() {
    // World

    auto world = random_scene();

    // Camera

    point3 lookfrom(13,2,3);
    point3 lookat(0,0,0);
    vec3 vup(0,1,0);
    auto dist_to_focus = 10.0;
    auto aperture = 0.1;

    camera cam(lookfrom, lookat, vup, 20, aspect_ratio, aperture, dist_to_focus);

    // Render

    int number_of_threads = 4; // configuarable

    int image_size_in_bytes = sizeof(color) * image_width * image_height;
    color *rendered_image = (color *) mmap(nullptr, image_size_in_bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    std::cerr << "\nStart rendering with " << number_of_threads << " processes.\n";

    render_image(cam, world, rendered_image, number_of_threads);

    // Write to std::cout

    std::cout << "P3\n" << image_width << ' ' << image_height << "\n255\n";

    for (int i = (image_width * image_height) - 1; i > -1; i--) {
        write_color(std::cout, rendered_image[i], samples_per_pixel);
    }

    std::cerr << "\nDone.\n";
}
