#
# Copyright (C) 2017, Collabora Ltd.
#   Author: Justin Kim <justin.kim@collabora.com>
#
# Copyright (C) 2026, KIMRIHYEON <dlgus8648@naver.com>
#   - Camera2 NDK 마이그레이션 및 ahc2src 플러그인 통합
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation
# version 2.1 of the License.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA

APP_ABI = arm64-v8a armeabi-v7a x86 x86_64

APP_PLATFORM = android-26

APP_STL = c++_shared

ifeq ($(NDK_DEBUG),1)
  APP_OPTIM := debug
endif
