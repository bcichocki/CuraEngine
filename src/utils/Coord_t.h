//Copyright (c) 2018 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef UTILS_COORD_T_H
#define UTILS_COORD_T_H


//Include Clipper to get the ClipperLib::IntPoint definition, which we reuse as Point definition.
#include <clipper/clipper.hpp>

namespace cura
{

using coord_t = ClipperLib::cInt;

static constexpr double INT2MM(coord_t n) noexcept { return (double(n) / 1000.0); }

static constexpr double INT2MM2(coord_t n) noexcept { return (double(n) / 1000000.0); }

static constexpr coord_t MM2INT(double n) noexcept { return (coord_t((n) * 1000 + 0.5 * (((n) > 0) - ((n) < 0)))); }

static constexpr coord_t MM2_2INT(double n) noexcept { return (coord_t((n) * 1000000 + 0.5 * (((n) > 0) - ((n) < 0)))); }

static constexpr coord_t MM3_2INT(double n) noexcept { return (coord_t((n) * 1000000000 + 0.5 * (((n) > 0) - ((n) < 0)))); }

#define INT2MICRON(n) ((n) / 1)
#define MICRON2INT(n) ((n) * 1)

} // namespace cura


#endif // UTILS_COORD_T_H
