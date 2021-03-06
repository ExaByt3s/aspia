//
// Aspia Project
// Copyright (C) 2018 Dmitry Chapyshev <dmitry@aspia.ru>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//

#ifndef CODEC__SCOPED_VPX_CODEC_H
#define CODEC__SCOPED_VPX_CODEC_H

#include <memory>

extern "C"
{
typedef struct vpx_codec_ctx vpx_codec_ctx_t;
}

namespace codec {

struct VpxCodecDeleter
{
    void operator()(vpx_codec_ctx_t* codec);
};

using ScopedVpxCodec = std::unique_ptr<vpx_codec_ctx_t, VpxCodecDeleter>;

} // namespace codec

#endif // CODEC__SCOPED_VPX_CODEC_H
