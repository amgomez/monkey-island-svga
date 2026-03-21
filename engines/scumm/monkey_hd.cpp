/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this source distribution.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 */

#include "common/algorithm.h"
#include "common/array.h"
#include "common/stream.h"
#include "engines/scumm/monkey_hd.h"
#include "engines/scumm/object.h"
#include "engines/scumm/scumm.h"
#include "graphics/scaler/scalebit.h"
#include "graphics/scaler/scale2x.h"
#include "graphics/scaler/xbrz.h"
#include "image/jpeg.h"
#include "image/png.h"

namespace Scumm {

namespace {

static bool parseRoomFromBackgroundName(const Common::String &name, int &room) {
	if (sscanf(name.c_str(), "%4d_", &room) != 1)
		return false;

	return room >= 0;
}

static bool parseRoomObjectFromName(const Common::String &name, int &room, int &objectId) {
	if (sscanf(name.c_str(), "%4d_%4d_IM01.png", &room, &objectId) != 2)
		return false;

	return room >= 0 && objectId >= 0;
}

static void blitOpaqueClipped(const Graphics::Surface &src, const Common::Rect &srcRect, Graphics::Surface &dst, const Common::Point &dstPos) {
	Common::Rect clippedSrc = srcRect;
	clippedSrc.clip(src.getRect());
	if (clippedSrc.isEmpty())
		return;

	int dstX = dstPos.x;
	int dstY = dstPos.y;

	if (dstX < 0) {
		clippedSrc.left -= dstX;
		dstX = 0;
	}
	if (dstY < 0) {
		clippedSrc.top -= dstY;
		dstY = 0;
	}
	if (dstX + clippedSrc.width() > dst.w)
		clippedSrc.right -= (dstX + clippedSrc.width()) - dst.w;
	if (dstY + clippedSrc.height() > dst.h)
		clippedSrc.bottom -= (dstY + clippedSrc.height()) - dst.h;

	if (clippedSrc.isEmpty())
		return;

	const int rowBytes = clippedSrc.width() * src.format.bytesPerPixel;
	for (int y = 0; y < clippedSrc.height(); ++y) {
		const byte *srcRow = (const byte *)src.getBasePtr(clippedSrc.left, clippedSrc.top + y);
		byte *dstRow = (byte *)dst.getBasePtr(dstX, dstY + y);
		memcpy(dstRow, srcRow, rowBytes);
	}
}

static void blitKeyedClipped(const Graphics::Surface &src, const Common::Rect &srcRect, Graphics::Surface &dst, const Common::Point &dstPos, byte transparentColor) {
	Common::Rect clippedSrc = srcRect;
	clippedSrc.clip(src.getRect());
	if (clippedSrc.isEmpty())
		return;

	int dstX = dstPos.x;
	int dstY = dstPos.y;

	if (dstX < 0) {
		clippedSrc.left -= dstX;
		dstX = 0;
	}
	if (dstY < 0) {
		clippedSrc.top -= dstY;
		dstY = 0;
	}
	if (dstX + clippedSrc.width() > dst.w)
		clippedSrc.right -= (dstX + clippedSrc.width()) - dst.w;
	if (dstY + clippedSrc.height() > dst.h)
		clippedSrc.bottom -= (dstY + clippedSrc.height()) - dst.h;

	if (clippedSrc.isEmpty())
		return;

	if (src.format.bytesPerPixel != 1 || dst.format.bytesPerPixel != 1) {
		blitOpaqueClipped(src, clippedSrc, dst, Common::Point(dstX, dstY));
		return;
	}

	for (int y = 0; y < clippedSrc.height(); ++y) {
		const byte *srcRow = (const byte *)src.getBasePtr(clippedSrc.left, clippedSrc.top + y);
		byte *dstRow = (byte *)dst.getBasePtr(dstX, dstY + y);
		for (int x = 0; x < clippedSrc.width(); ++x) {
			if (srcRow[x] != transparentColor)
				dstRow[x] = srcRow[x];
		}
	}
}

static void buildOutputPalette(const byte *paletteData, const Graphics::PixelFormat &dstFormat, uint32 outputPalette[256]) {
	for (int i = 0; i < 256; ++i) {
		const int color = i * 3;
		outputPalette[i] = dstFormat.ARGBToColor(0xFF, paletteData[color + 0], paletteData[color + 1], paletteData[color + 2]);
	}
}

static void blitOpaqueMappedClipped(const Graphics::Surface &src, const Common::Rect &srcRect, Graphics::Surface &dst, const Common::Point &dstPos, const uint32 outputPalette[256]) {
	Common::Rect clippedSrc = srcRect;
	clippedSrc.clip(src.getRect());
	if (clippedSrc.isEmpty())
		return;

	int dstX = dstPos.x;
	int dstY = dstPos.y;

	if (dstX < 0) {
		clippedSrc.left -= dstX;
		dstX = 0;
	}
	if (dstY < 0) {
		clippedSrc.top -= dstY;
		dstY = 0;
	}
	if (dstX + clippedSrc.width() > dst.w)
		clippedSrc.right -= (dstX + clippedSrc.width()) - dst.w;
	if (dstY + clippedSrc.height() > dst.h)
		clippedSrc.bottom -= (dstY + clippedSrc.height()) - dst.h;

	if (clippedSrc.isEmpty())
		return;

	for (int y = 0; y < clippedSrc.height(); ++y) {
		const byte *srcRow = (const byte *)src.getBasePtr(clippedSrc.left, clippedSrc.top + y);
		uint32 *dstRow = (uint32 *)dst.getBasePtr(dstX, dstY + y);
		for (int x = 0; x < clippedSrc.width(); ++x)
			dstRow[x] = outputPalette[srcRow[x]];
	}
}

static void blitKeyedMappedClipped(const Graphics::Surface &src, const Common::Rect &srcRect, Graphics::Surface &dst, const Common::Point &dstPos, byte transparentColor, const uint32 outputPalette[256]) {
	Common::Rect clippedSrc = srcRect;
	clippedSrc.clip(src.getRect());
	if (clippedSrc.isEmpty())
		return;

	int dstX = dstPos.x;
	int dstY = dstPos.y;

	if (dstX < 0) {
		clippedSrc.left -= dstX;
		dstX = 0;
	}
	if (dstY < 0) {
		clippedSrc.top -= dstY;
		dstY = 0;
	}
	if (dstX + clippedSrc.width() > dst.w)
		clippedSrc.right -= (dstX + clippedSrc.width()) - dst.w;
	if (dstY + clippedSrc.height() > dst.h)
		clippedSrc.bottom -= (dstY + clippedSrc.height()) - dst.h;

	if (clippedSrc.isEmpty())
		return;

	for (int y = 0; y < clippedSrc.height(); ++y) {
		const byte *srcRow = (const byte *)src.getBasePtr(clippedSrc.left, clippedSrc.top + y);
		uint32 *dstRow = (uint32 *)dst.getBasePtr(dstX, dstY + y);
		for (int x = 0; x < clippedSrc.width(); ++x) {
			if (srcRow[x] != transparentColor)
				dstRow[x] = outputPalette[srcRow[x]];
		}
	}
}

static void blitRGBAClipped(const Graphics::Surface &src, const Common::Rect &srcRect, Graphics::Surface &dst, const Common::Point &dstPos, bool blendAlpha) {
	assert(src.format.bytesPerPixel == 4);
	assert(dst.format.bytesPerPixel == 4);

	Common::Rect clippedSrc = srcRect;
	clippedSrc.clip(src.getRect());
	if (clippedSrc.isEmpty())
		return;

	int dstX = dstPos.x;
	int dstY = dstPos.y;

	if (dstX < 0) {
		clippedSrc.left -= dstX;
		dstX = 0;
	}
	if (dstY < 0) {
		clippedSrc.top -= dstY;
		dstY = 0;
	}
	if (dstX + clippedSrc.width() > dst.w)
		clippedSrc.right -= (dstX + clippedSrc.width()) - dst.w;
	if (dstY + clippedSrc.height() > dst.h)
		clippedSrc.bottom -= (dstY + clippedSrc.height()) - dst.h;

	if (clippedSrc.isEmpty())
		return;

	for (int y = 0; y < clippedSrc.height(); ++y) {
		const uint32 *srcRow = (const uint32 *)src.getBasePtr(clippedSrc.left, clippedSrc.top + y);
		uint32 *dstRow = (uint32 *)dst.getBasePtr(dstX, dstY + y);
		for (int x = 0; x < clippedSrc.width(); ++x) {
			byte srcA, srcR, srcG, srcB;
			src.format.colorToARGB(srcRow[x], srcA, srcR, srcG, srcB);
			if (!srcA)
				continue;

			if (!blendAlpha || srcA == 0xFF) {
				dstRow[x] = dst.format.ARGBToColor(0xFF, srcR, srcG, srcB);
				continue;
			}

			byte dstA, dstR, dstG, dstB;
			dst.format.colorToARGB(dstRow[x], dstA, dstR, dstG, dstB);
			const byte invA = 0xFF - srcA;
			dstRow[x] = dst.format.ARGBToColor(0xFF,
				(srcR * srcA + dstR * invA) / 255,
				(srcG * srcA + dstG * invA) / 255,
				(srcB * srcA + dstB * invA) / 255);
		}
	}
}

static byte mapARGBToPaletteColor(const Graphics::Palette &activePalette, Common::HashMap<uint32, byte> &colorCache, uint32 argb, bool transparentAlphaToZero);

static void blitRGBAToCLUT8Clipped(const Graphics::Surface &src, const Common::Rect &srcRect, Graphics::Surface &dst, const Common::Point &dstPos,
		const Graphics::Palette &activePalette, Common::HashMap<uint32, byte> &colorCache, bool useAlphaThreshold) {
	assert(src.format.bytesPerPixel == 4);
	assert(dst.format.bytesPerPixel == 1);

	Common::Rect clippedSrc = srcRect;
	clippedSrc.clip(src.getRect());
	if (clippedSrc.isEmpty())
		return;

	int dstX = dstPos.x;
	int dstY = dstPos.y;

	if (dstX < 0) {
		clippedSrc.left -= dstX;
		dstX = 0;
	}
	if (dstY < 0) {
		clippedSrc.top -= dstY;
		dstY = 0;
	}
	if (dstX + clippedSrc.width() > dst.w)
		clippedSrc.right -= (dstX + clippedSrc.width()) - dst.w;
	if (dstY + clippedSrc.height() > dst.h)
		clippedSrc.bottom -= (dstY + clippedSrc.height()) - dst.h;

	if (clippedSrc.isEmpty())
		return;

	for (int y = 0; y < clippedSrc.height(); ++y) {
		const uint32 *srcRow = (const uint32 *)src.getBasePtr(clippedSrc.left, clippedSrc.top + y);
		byte *dstRow = (byte *)dst.getBasePtr(dstX, dstY + y);
		for (int x = 0; x < clippedSrc.width(); ++x) {
			byte a, r, g, b;
			src.format.colorToARGB(srcRow[x], a, r, g, b);
			if (useAlphaThreshold && a < 0x80)
				continue;

			const uint32 argb = (uint32(a) << 24) | (uint32(r) << 16) | (uint32(g) << 8) | uint32(b);
			dstRow[x] = mapARGBToPaletteColor(activePalette, colorCache, argb, false);
		}
	}
}

static void plotScaledPixel(byte color, byte *dst, int dstPitch, int scale) {
	for (int y = 0; y < scale; ++y)
		memset(dst + y * dstPitch, color, scale);
}

static void scale2xBitmap8(byte *dst, int dstPitch, const byte *src, int srcPitch, int width, int height) {
	for (int y = 0; y < height; ++y) {
		const int prevY = (y > 0) ? (y - 1) : y;
		const int nextY = (y + 1 < height) ? (y + 1) : y;
		const byte *src0 = src + prevY * srcPitch;
		const byte *src1 = src + y * srcPitch;
		const byte *src2 = src + nextY * srcPitch;
		byte *dst0 = dst + (y * 2) * dstPitch;
		byte *dst1 = dst0 + dstPitch;
		scale2x_8_def(dst0, dst1, src0, src1, src2, width);
	}
}

static void scale4xBitmap8(byte *dst, int dstPitch, const byte *src, int srcPitch, int width, int height) {
	Common::Array<byte> temp(width * 2 * height * 2, (byte)CHARSET_MASK_TRANSPARENCY);
	scale2xBitmap8(temp.data(), width * 2, src, srcPitch, width, height);
	scale2xBitmap8(dst, dstPitch, temp.data(), width * 2, width * 2, height * 2);
}

static void advMameScale4xBitmap8(byte *dst, int dstPitch, const byte *src, int srcPitch, int width, int height) {
	if (scale_precondition(4, 1, width, height) != 0) {
		scale4xBitmap8(dst, dstPitch, src, srcPitch, width, height);
		return;
	}

	Common::Array<byte> padded(width * (height + 4));
	const byte *firstRow = src;
	const byte *lastRow = src + (height - 1) * srcPitch;
	memcpy(padded.data(), firstRow, width);
	memcpy(padded.data() + width, firstRow, width);
	for (int y = 0; y < height; ++y)
		memcpy(padded.data() + (y + 2) * width, src + y * srcPitch, width);
	memcpy(padded.data() + (height + 2) * width, lastRow, width);
	memcpy(padded.data() + (height + 3) * width, lastRow, width);

	scale(4, dst, dstPitch, padded.data(), width, 1, width, height);
}

static void buildDiffMask(const byte *frontBase, const byte *backBase, int srcPitch, int srcX, int srcY, int width, int height, Common::Array<byte> &mask) {
	mask.resize(width * height);

	for (int y = 0; y < height; ++y) {
		const byte *frontRow = frontBase + (srcY + y) * srcPitch + srcX;
		const byte *backRow = backBase + (srcY + y) * srcPitch + srcX;
		byte *maskRow = mask.data() + y * width;
		for (int x = 0; x < width; ++x)
			maskRow[x] = (frontRow[x] != backRow[x]) ? 1 : 0;
	}
}

static void fillMaskHoles(const Common::Array<byte> &srcMask, int width, int height, Common::Array<byte> &dstMask) {
	dstMask = srcMask;

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const int idx = y * width + x;
			if (srcMask[idx])
				continue;

			int neighbors = 0;
			for (int yy = MAX(0, y - 1); yy <= MIN(height - 1, y + 1); ++yy) {
				for (int xx = MAX(0, x - 1); xx <= MIN(width - 1, x + 1); ++xx) {
					if (xx == x && yy == y)
						continue;
					neighbors += srcMask[yy * width + xx] ? 1 : 0;
				}
			}

			if (neighbors >= 5)
				dstMask[idx] = 1;
		}
	}
}

static void findConnectedComponents(const Common::Array<byte> &mask, int width, int height, Common::Array<int> &labels, Common::Array<Common::Rect> &components) {
	labels.resize(width * height);
	Common::fill(labels.begin(), labels.end(), -1);
	components.clear();

	Common::Array<int> queue(width * height);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const int start = y * width + x;
			if (!mask[start] || labels[start] != -1)
				continue;

			int head = 0;
			int tail = 0;
			queue[tail++] = start;
			labels[start] = components.size();

			int left = x;
			int right = x + 1;
			int top = y;
			int bottom = y + 1;

			while (head < tail) {
				const int idx = queue[head++];
				const int cx = idx % width;
				const int cy = idx / width;

				left = MIN(left, cx);
				right = MAX(right, cx + 1);
				top = MIN(top, cy);
				bottom = MAX(bottom, cy + 1);

				for (int yy = MAX(0, cy - 1); yy <= MIN(height - 1, cy + 1); ++yy) {
					for (int xx = MAX(0, cx - 1); xx <= MIN(width - 1, cx + 1); ++xx) {
						const int nidx = yy * width + xx;
						if (!mask[nidx] || labels[nidx] != -1)
							continue;
						labels[nidx] = components.size();
						queue[tail++] = nidx;
					}
				}
			}

			components.push_back(Common::Rect(left, top, right, bottom));
		}
	}
}

static bool componentTouchesPixel(const Common::Array<int> &labels, int width, int height, int x, int y, int componentId, int radius) {
	for (int yy = MAX(0, y - radius); yy <= MIN(height - 1, y + radius); ++yy) {
		for (int xx = MAX(0, x - radius); xx <= MIN(width - 1, x + radius); ++xx) {
			if (labels[yy * width + xx] == componentId)
				return true;
		}
	}

	return false;
}

static void blendARGBPatchToRGBA(Graphics::Surface &dstSurface, const uint32 *srcArgb, int srcWidth, int srcHeight, int dstX, int dstY) {
	assert(dstSurface.format.bytesPerPixel == 4);

	int srcStartX = 0;
	int srcStartY = 0;
	if (dstX < 0) {
		srcStartX = -dstX;
		dstX = 0;
	}
	if (dstY < 0) {
		srcStartY = -dstY;
		dstY = 0;
	}

	int copyWidth = srcWidth - srcStartX;
	int copyHeight = srcHeight - srcStartY;
	if (dstX + copyWidth > dstSurface.w)
		copyWidth = dstSurface.w - dstX;
	if (dstY + copyHeight > dstSurface.h)
		copyHeight = dstSurface.h - dstY;

	if (copyWidth <= 0 || copyHeight <= 0)
		return;

	for (int y = 0; y < copyHeight; ++y) {
		const uint32 *srcRow = srcArgb + (srcStartY + y) * srcWidth + srcStartX;
		uint32 *dstRow = (uint32 *)dstSurface.getBasePtr(dstX, dstY + y);
		for (int x = 0; x < copyWidth; ++x) {
			const uint32 srcPixel = srcRow[x];
			const byte srcA = (srcPixel >> 24) & 0xFF;
			if (!srcA)
				continue;

			const byte srcR = (srcPixel >> 16) & 0xFF;
			const byte srcG = (srcPixel >> 8) & 0xFF;
			const byte srcB = srcPixel & 0xFF;
			if (srcA == 0xFF) {
				dstRow[x] = dstSurface.format.ARGBToColor(0xFF, srcR, srcG, srcB);
				continue;
			}

			byte dstA, dstR, dstG, dstB;
			dstSurface.format.colorToARGB(dstRow[x], dstA, dstR, dstG, dstB);
			const byte invA = 0xFF - srcA;
			dstRow[x] = dstSurface.format.ARGBToColor(0xFF,
				(srcR * srcA + dstR * invA) / 255,
				(srcG * srcA + dstG * invA) / 255,
				(srcB * srcA + dstB * invA) / 255);
		}
	}
}

static void overlayScaledBitmapDiff(const byte *paletteData, Graphics::Surface &dstSurface,
		const byte *frontBase, const byte *backBase, int srcPitch, int srcWidth, int srcHeight,
		int srcX, int srcY, int width, int height, int scale) {
	if (width <= 0 || height <= 0 || scale <= 0)
		return;

	if (scale == 1) {
		for (int y = 0; y < height; ++y) {
			const byte *frontRow = frontBase + (srcY + y) * srcPitch + srcX;
			const byte *backRow = backBase + (srcY + y) * srcPitch + srcX;
			if (dstSurface.format.bytesPerPixel == 4) {
				uint32 *dstRow = (uint32 *)dstSurface.getBasePtr(0, y);
				uint32 outputPalette[256];
				buildOutputPalette(paletteData, dstSurface.format, outputPalette);
				for (int x = 0; x < width; ++x) {
					if (frontRow[x] != backRow[x])
						dstRow[x] = outputPalette[frontRow[x]];
				}
			} else {
				byte *dstRow = (byte *)dstSurface.getBasePtr(0, y);
				for (int x = 0; x < width; ++x) {
					if (frontRow[x] != backRow[x])
						dstRow[x] = frontRow[x];
				}
			}
		}
		return;
	}

	Graphics::Palette activePalette(paletteData, Graphics::PALETTE_COUNT);
	Common::HashMap<uint32, byte> colorCache;
	uint32 outputPalette[256];
	if (dstSurface.format.bytesPerPixel == 4)
		buildOutputPalette(paletteData, dstSurface.format, outputPalette);

	if (scale == 4) {
		Common::Array<byte> diffMask;
		buildDiffMask(frontBase, backBase, srcPitch, srcX, srcY, width, height, diffMask);
		bool hasAnyDiff = false;
		for (uint i = 0; i < diffMask.size(); ++i) {
			if (diffMask[i]) {
				hasAnyDiff = true;
				break;
			}
		}
		if (!hasAnyDiff)
			return;

		Common::Array<byte> seedMask;
		fillMaskHoles(diffMask, width, height, seedMask);

		Common::Array<int> labels;
		Common::Array<Common::Rect> components;
		findConnectedComponents(seedMask, width, height, labels, components);
		if (components.empty())
			return;

		const int maskDilate = 1;
		const int contextPad = 2;

		if (dstSurface.format.bytesPerPixel == 4) {
			for (uint componentId = 0; componentId < components.size(); ++componentId) {
				const Common::Rect &bounds = components[componentId];
				const int patchLeft = MAX(0, srcX + bounds.left - contextPad);
				const int patchTop = MAX(0, srcY + bounds.top - contextPad);
				const int patchRight = MIN(srcWidth, srcX + bounds.right + contextPad);
				const int patchBottom = MIN(srcHeight, srcY + bounds.bottom + contextPad);
				const int patchWidth = patchRight - patchLeft;
				const int patchHeight = patchBottom - patchTop;
				if (patchWidth <= 0 || patchHeight <= 0)
					continue;

				Common::Array<uint32> overlayArgb(patchWidth * patchHeight, 0u);
				for (int py = 0; py < patchHeight; ++py) {
					const int globalY = patchTop + py;
					const byte *frontRow = frontBase + globalY * srcPitch + patchLeft;
					uint32 *overlayRow = overlayArgb.data() + py * patchWidth;
					for (int px = 0; px < patchWidth; ++px) {
						const int globalX = patchLeft + px;
						const int color = frontRow[px] * 3;
						const byte r = paletteData[color + 0];
						const byte g = paletteData[color + 1];
						const byte b = paletteData[color + 2];

						byte a = 0;
						const int localX = globalX - srcX;
						const int localY = globalY - srcY;
						if (localX >= 0 && localX < width && localY >= 0 && localY < height &&
								componentTouchesPixel(labels, width, height, localX, localY, componentId, maskDilate))
							a = 0xFF;

						overlayRow[px] = (uint32(a) << 24) | (uint32(r) << 16) | (uint32(g) << 8) | uint32(b);
					}
				}

				Common::Array<uint32> scaledOverlay(patchWidth * patchHeight * 16);
				xbrz::scale(4, overlayArgb.data(), scaledOverlay.data(), patchWidth, patchHeight, xbrz::ColorFormat::ARGB);
				blendARGBPatchToRGBA(dstSurface, scaledOverlay.data(), patchWidth * 4, patchHeight * 4,
					(patchLeft - srcX) * 4, (patchTop - srcY) * 4);
			}
			return;
		}

		const int pad = 2;
		const int patchLeft = MAX(0, srcX - pad);
		const int patchTop = MAX(0, srcY - pad);
		const int patchRight = MIN(srcWidth, srcX + width + pad);
		const int patchBottom = MIN(srcHeight, srcY + height + pad);
		const int patchWidth = patchRight - patchLeft;
		const int patchHeight = patchBottom - patchTop;
		const int cropLeft = (srcX - patchLeft) * scale;
		const int cropTop = (srcY - patchTop) * scale;

		Common::Array<uint32> overlayArgb(patchWidth * patchHeight, 0u);
		for (int y = 0; y < patchHeight; ++y) {
			const byte *frontRow = frontBase + (patchTop + y) * srcPitch + patchLeft;
			const byte *backRow = backBase + (patchTop + y) * srcPitch + patchLeft;
			uint32 *overlayRow = overlayArgb.data() + y * patchWidth;
			for (int x = 0; x < patchWidth; ++x) {
				const int color = frontRow[x] * 3;
				const byte r = paletteData[color + 0];
				const byte g = paletteData[color + 1];
				const byte b = paletteData[color + 2];
				const byte a = (frontRow[x] != backRow[x]) ? 0xFF : 0x00;
				overlayRow[x] = (uint32(a) << 24) | (uint32(r) << 16) | (uint32(g) << 8) | uint32(b);
			}
		}

		Common::Array<uint32> scaledOverlay(patchWidth * patchHeight * 16);
		xbrz::scale(4, overlayArgb.data(), scaledOverlay.data(), patchWidth, patchHeight, xbrz::ColorFormat::ARGB);

		const int scaledWidth = width * 4;
		const int scaledHeight = height * 4;
		for (int y = 0; y < scaledHeight; ++y) {
			byte *dstRow = (byte *)dstSurface.getBasePtr(0, y);
			const uint32 *overlayRow = scaledOverlay.data() + (cropTop + y) * (patchWidth * 4) + cropLeft;
			for (int x = 0; x < scaledWidth; ++x) {
				const uint32 srcArgb = overlayRow[x];
				const byte srcA = (srcArgb >> 24) & 0xFF;
				if (!srcA)
					continue;

				if (srcA == 0xFF) {
					dstRow[x] = mapARGBToPaletteColor(activePalette, colorCache, srcArgb, false);
					continue;
				}

				const int dstColor = dstRow[x] * 3;
				const uint32 dstArgb = (0xFFu << 24) | (uint32(paletteData[dstColor + 0]) << 16) |
					(uint32(paletteData[dstColor + 1]) << 8) | uint32(paletteData[dstColor + 2]);
				const byte srcR = (srcArgb >> 16) & 0xFF;
				const byte srcG = (srcArgb >> 8) & 0xFF;
				const byte srcB = srcArgb & 0xFF;
				const byte dstR = (dstArgb >> 16) & 0xFF;
				const byte dstG = (dstArgb >> 8) & 0xFF;
				const byte dstB = dstArgb & 0xFF;
				const byte invA = 0xFF - srcA;
				const uint32 blendedArgb = (0xFFu << 24) |
					(uint32((srcR * srcA + dstR * invA) / 255) << 16) |
					(uint32((srcG * srcA + dstG * invA) / 255) << 8) |
					uint32((srcB * srcA + dstB * invA) / 255);
				dstRow[x] = mapARGBToPaletteColor(activePalette, colorCache, blendedArgb, false);
			}
		}
		return;
	}

	Common::Array<byte> frontPatch(width * height);
	Common::Array<byte> backPatch(width * height);
	for (int y = 0; y < height; ++y) {
		memcpy(frontPatch.data() + y * width, frontBase + (srcY + y) * srcPitch + srcX, width);
		memcpy(backPatch.data() + y * width, backBase + (srcY + y) * srcPitch + srcX, width);
	}

	const int scaledWidth = width * scale;
	const int scaledHeight = height * scale;
	Common::Array<byte> scaledFront(scaledWidth * scaledHeight);
	Common::Array<byte> scaledBack(scaledWidth * scaledHeight);

	if (scale == 2) {
		scale2xBitmap8(scaledFront.data(), scaledWidth, frontPatch.data(), width, width, height);
		scale2xBitmap8(scaledBack.data(), scaledWidth, backPatch.data(), width, width, height);
	} else {
		for (int y = 0; y < height; ++y) {
			for (int sy = 0; sy < scale; ++sy) {
				byte *frontRow = scaledFront.data() + (y * scale + sy) * scaledWidth;
				byte *backRow = scaledBack.data() + (y * scale + sy) * scaledWidth;
				for (int x = 0; x < width; ++x) {
					memset(frontRow + x * scale, frontPatch[y * width + x], scale);
					memset(backRow + x * scale, backPatch[y * width + x], scale);
				}
			}
		}
	}

	for (int y = 0; y < scaledHeight; ++y) {
		const byte *frontRow = scaledFront.data() + y * scaledWidth;
		const byte *backRow = scaledBack.data() + y * scaledWidth;
		if (dstSurface.format.bytesPerPixel == 4) {
			uint32 *dstRow = (uint32 *)dstSurface.getBasePtr(0, y);
			for (int x = 0; x < scaledWidth; ++x) {
				if (frontRow[x] != backRow[x])
					dstRow[x] = outputPalette[frontRow[x]];
			}
		} else {
			byte *dstRow = (byte *)dstSurface.getBasePtr(0, y);
			for (int x = 0; x < scaledWidth; ++x) {
				if (frontRow[x] != backRow[x])
					dstRow[x] = frontRow[x];
			}
		}
	}
}

static void convertSurfaceToRGBA32(const Graphics::Surface &src, const Graphics::Palette *srcPalette, Graphics::ManagedSurface &rgba, bool hasTransparentColor = false, uint32 transparentColor = 0) {
	rgba.free();
	rgba.create(src.w, src.h, Graphics::PixelFormat::createFormatRGBA32());

	for (int y = 0; y < src.h; ++y) {
		for (int x = 0; x < src.w; ++x) {
			byte a = 255;
			byte r = 0;
			byte g = 0;
			byte b = 0;

			if (src.format.bytesPerPixel == 1 && srcPalette && !srcPalette->empty()) {
				const uint32 index = src.getPixel(x, y);
				if (hasTransparentColor && index == transparentColor)
					a = 0;
				if (index < srcPalette->size())
					srcPalette->get(index, r, g, b);
			} else {
				src.format.colorToARGB(src.getPixel(x, y), a, r, g, b);
			}

			*((uint32 *)rgba.getBasePtr(x, y)) = rgba.format.ARGBToColor(a, r, g, b);
		}
	}
}

static bool surfaceHasAlpha(const Graphics::Surface &surface) {
	if (surface.format.bytesPerPixel != 4 || surface.format.aLoss == 8)
		return false;

	for (int y = 0; y < surface.h; ++y) {
		const uint32 *row = (const uint32 *)surface.getBasePtr(0, y);
		for (int x = 0; x < surface.w; ++x) {
			byte a, r, g, b;
			surface.format.colorToARGB(row[x], a, r, g, b);
			if (a != 0xFF)
				return true;
		}
	}

	return false;
}

static bool decodeImageDimensions(const Common::FSNode &node, int &width, int &height) {
	Common::SeekableReadStream *stream = node.createReadStream();
	if (!stream)
		return false;

	const Common::String name = node.getName();
	bool loaded = false;
	const Graphics::Surface *surface = nullptr;

	if (name.hasSuffixIgnoreCase(".png")) {
		Image::PNGDecoder decoder;
		decoder.setKeepTransparencyPaletted(true);
		loaded = decoder.loadStream(*stream);
		surface = decoder.getSurface();
		if (loaded && surface) {
			width = surface->w;
			height = surface->h;
			delete stream;
			return true;
		}
	} else if (name.hasSuffixIgnoreCase(".jpg") || name.hasSuffixIgnoreCase(".jpeg")) {
		Image::JPEGDecoder decoder;
		loaded = decoder.loadStream(*stream);
		surface = decoder.getSurface();
		if (loaded && surface) {
			width = surface->w;
			height = surface->h;
			delete stream;
			return true;
		}
	}

	delete stream;
	return false;
}

static byte mapARGBToPaletteColor(const Graphics::Palette &activePalette, Common::HashMap<uint32, byte> &colorCache, uint32 argb, bool transparentAlphaToZero = false) {
	const byte a = (argb >> 24) & 0xFF;
	if (transparentAlphaToZero && a < 0x80)
		return 0;

	const uint32 colorKey = argb & 0x00FFFFFF;
	if (!colorCache.contains(colorKey)) {
		const byte r = (argb >> 16) & 0xFF;
		const byte g = (argb >> 8) & 0xFF;
		const byte b = argb & 0xFF;
		const uint mapped = activePalette.find(r, g, b);
		colorCache[colorKey] = (mapped != Graphics::Palette::npos) ? mapped : activePalette.findBestColor(r, g, b);
	}

	return colorCache[colorKey];
}

} // End of anonymous namespace

MonkeyHdRenderer::MonkeyHdRenderer() {
}

MonkeyHdRenderer::~MonkeyHdRenderer() {
	clearRoomCache();
}

bool MonkeyHdRenderer::indexRoot(const Common::FSNode &root) {
	if (!root.exists() || !root.isDirectory())
		return false;

	bool indexedAny = false;

	_backgroundsDir = root.getChild("backgrounds");
	_objectsDir = root.getChild("objects");
	Common::FSList files;
	if (_backgroundsDir.exists() && _backgroundsDir.isDirectory() && _backgroundsDir.getChildren(files, Common::FSNode::kListFilesOnly, false)) {
		for (Common::FSList::const_iterator it = files.begin(); it != files.end(); ++it) {
			int room = -1;
			if (parseRoomFromBackgroundName(it->getName(), room)) {
				_backgroundPaths[room] = it->getPath();
				indexedAny = true;
			}
		}
	}

	files.clear();
	if (_objectsDir.exists() && _objectsDir.isDirectory() && _objectsDir.getChildren(files, Common::FSNode::kListFilesOnly, false)) {
		for (Common::FSList::const_iterator it = files.begin(); it != files.end(); ++it) {
			int room = -1;
			int objectId = -1;
			if (parseRoomObjectFromName(it->getName(), room, objectId)) {
				_objectPaths[makeObjectKey(room, objectId)] = it->getPath();
				indexedAny = true;
			}
		}
	}

	return indexedAny;
}

bool MonkeyHdRenderer::init(const Common::FSNode &root) {
	_backgroundPaths.clear();
	_baseBackgroundPaths.clear();
	_objectPaths.clear();
	_baseObjectPaths.clear();
	_inventoryVerbs.clear();
	clearRoomCache();
	_initialized = false;

	if (!root.exists() || !root.isDirectory())
		return false;

	indexRoot(root);

	_baseBackgroundPaths = _backgroundPaths;
	_baseObjectPaths = _objectPaths;
	_initialized = true;

	if (!_backgroundPaths.empty() || !_objectPaths.empty())
		warning("Monkey HD 4X: using custom assets from '%s'", root.getPath().toString(Common::Path::kNativeSeparator).c_str());

	return true;
}

bool MonkeyHdRenderer::isReady() const {
	return _initialized;
}

void MonkeyHdRenderer::rememberVerbObject(int verbSlot, int roomResource, int objectId) {
	if (verbSlot <= 0)
		return;

	if (roomResource != 99 || objectId <= 0) {
		forgetVerbObject(verbSlot);
		return;
	}

	InventoryVerbState &state = _inventoryVerbs[verbSlot];
	state.objectId = objectId;
	state.visible = false;
	state.rect = Common::Rect();
}

void MonkeyHdRenderer::forgetVerbObject(int verbSlot) {
	if (verbSlot <= 0)
		return;

	_inventoryVerbs.erase(verbSlot);
}

bool MonkeyHdRenderer::isInventoryVerb(int verbSlot, int objectId) const {
	if (verbSlot <= 0 || objectId <= 0 || !_inventoryVerbs.contains(verbSlot))
		return false;

	return _inventoryVerbs.getVal(verbSlot).objectId == objectId;
}

bool MonkeyHdRenderer::activateInventoryVerb(int verbSlot, int objectId, const Common::Rect &screenRect) {
	if (!isInventoryVerb(verbSlot, objectId))
		return false;

	InventoryVerbState &state = _inventoryVerbs[verbSlot];
	if (screenRect.width() != 40 || screenRect.height() != 24 || screenRect.left < 128) {
		state.visible = false;
		return false;
	}

	state.visible = true;
	state.rect = screenRect;
	return true;
}

void MonkeyHdRenderer::deactivateInventoryVerb(int verbSlot) {
	if (verbSlot <= 0 || !_inventoryVerbs.contains(verbSlot))
		return;

	_inventoryVerbs[verbSlot].visible = false;
}

Common::String MonkeyHdRenderer::makeObjectKey(int room, int objectId) const {
	return Common::String::format("%04d_%04d_IM01.png", room, objectId);
}

int MonkeyHdRenderer::resolveBackgroundId(const ScummEngine &vm, int roomResource, const RoomPathMap &paths) const {
	int backgroundId = roomResource;
	if (!paths.contains(backgroundId) && vm._currentRoom > 0 && paths.contains(vm._currentRoom))
		backgroundId = vm._currentRoom;

	return backgroundId;
}

void MonkeyHdRenderer::clearRoomCache() {
	for (ObjectSurfaceMap::iterator it = _roomObjects.begin(); it != _roomObjects.end(); ++it)
		delete it->_value;
	_roomObjects.clear();
	_roomBackground.free();
	_roomBackgroundFromGame = false;
	_roomBackgroundHasAlpha = false;
	_loadedRoom = -1;
}

bool MonkeyHdRenderer::loadBackgroundPngRemapped(const Common::FSNode &node, Graphics::ManagedSurface &surface, int targetWidth, int targetHeight) {
	Common::SeekableReadStream *stream = node.createReadStream();
	if (!stream)
		return false;

	const Common::String name = node.getName();
	Graphics::ManagedSurface rgba;

	if (name.hasSuffixIgnoreCase(".png")) {
		Image::PNGDecoder decoder;
		decoder.setKeepTransparencyPaletted(true);
		const bool loaded = decoder.loadStream(*stream);
		if (loaded && decoder.getSurface()) {
			const Graphics::Palette *srcPalette = decoder.getPalette().empty() ? nullptr : &decoder.getPalette();
			const bool hasTransparentColor = decoder.hasTransparentColor();
			const uint32 transparentColor = hasTransparentColor ? decoder.getTransparentColor() : 0;
			convertSurfaceToRGBA32(*decoder.getSurface(), srcPalette, rgba, hasTransparentColor, transparentColor);
		}
	} else if (name.hasSuffixIgnoreCase(".jpg") || name.hasSuffixIgnoreCase(".jpeg")) {
		Image::JPEGDecoder decoder;
		const bool loaded = decoder.loadStream(*stream);
		if (loaded && decoder.getSurface())
			convertSurfaceToRGBA32(*decoder.getSurface(), nullptr, rgba, false, 0);
	}
	delete stream;

	if (rgba.empty())
		return false;

	const Graphics::ManagedSurface *rgbaSource = &rgba;
	Graphics::ManagedSurface *scaled = nullptr;

	if (targetWidth > 0 && targetHeight > 0 && (rgba.w != targetWidth || rgba.h != targetHeight)) {
		const bool useFiltering = (rgba.w > targetWidth || rgba.h > targetHeight);
		scaled = rgba.scale(targetWidth, targetHeight, useFiltering);
		if (!scaled)
			return false;

		warning("Monkey HD 4X: normalizing background '%s' from %dx%d to %dx%d",
			node.getName().c_str(), rgba.w, rgba.h, targetWidth, targetHeight);
		rgbaSource = scaled;
	}

	surface.copyFrom(*rgbaSource);
	_roomBackgroundHasAlpha = surfaceHasAlpha(surface);
	delete scaled;
	return true;
}

bool MonkeyHdRenderer::loadObjectPngRemapped(const Common::FSNode &node, LoadedObjectSurface &surface, int targetWidth, int targetHeight) {
	Common::SeekableReadStream *stream = node.createReadStream();
	if (!stream)
		return false;

	Image::PNGDecoder decoder;
	decoder.setKeepTransparencyPaletted(true);
	const bool loaded = decoder.loadStream(*stream);
	delete stream;

	if (!loaded || !decoder.getSurface())
		return false;

	const Graphics::Surface *src = decoder.getSurface();
	const Graphics::Palette *srcPalette = decoder.getPalette().empty() ? nullptr : &decoder.getPalette();
	const bool hasTransparentColor = decoder.hasTransparentColor();
	const uint32 transparentColor = hasTransparentColor ? decoder.getTransparentColor() : 0;

	Graphics::ManagedSurface rgba;
	convertSurfaceToRGBA32(*src, srcPalette, rgba, hasTransparentColor, transparentColor);

	const Graphics::ManagedSurface *rgbaSource = &rgba;
	Graphics::ManagedSurface *scaled = nullptr;

	if (targetWidth > 0 && targetHeight > 0 && (rgba.w != targetWidth || rgba.h != targetHeight)) {
		const bool useFiltering = (rgba.w > targetWidth || rgba.h > targetHeight);
		scaled = rgba.scale(targetWidth, targetHeight, useFiltering);
		if (!scaled)
			return false;

		warning("Monkey HD 4X: normalizing object '%s' from %dx%d to %dx%d",
			node.getName().c_str(), rgba.w, rgba.h, targetWidth, targetHeight);
		rgbaSource = scaled;
	}

	surface.surface.copyFrom(*rgbaSource);
	surface.hasAlpha = surfaceHasAlpha(surface.surface);
	delete scaled;
	return true;
}

bool MonkeyHdRenderer::loadBackgroundFromGame(ScummEngine &vm, Graphics::ManagedSurface &surface, int targetWidth, int targetHeight) {
	VirtScreen *vs = &vm._virtscr[kMainVirtScreen];
	if (!vs->backBuf || vm._roomWidth <= 0 || vs->h <= 0)
		return false;

	Graphics::Surface indexed;
	indexed.init(vm._roomWidth, vs->h, vs->pitch, vs->backBuf, Graphics::PixelFormat::createFormatCLUT8());
	Graphics::Palette activePalette(vm._currentPalette, Graphics::PALETTE_COUNT);

	Graphics::ManagedSurface rgba;
	convertSurfaceToRGBA32(indexed, &activePalette, rgba, false, 0);

	const Graphics::ManagedSurface *rgbaSource = &rgba;
	Graphics::ManagedSurface *scaled = nullptr;
	if (targetWidth > 0 && targetHeight > 0 && (rgba.w != targetWidth || rgba.h != targetHeight)) {
		scaled = rgba.scale(targetWidth, targetHeight, false);
		if (!scaled)
			return false;
		rgbaSource = scaled;
	}

	surface.copyFrom(*rgbaSource);
	delete scaled;
	_roomBackgroundHasAlpha = false;
	_roomBackgroundFromGame = true;
	return true;
}

bool MonkeyHdRenderer::loadObjectFromGame(ScummEngine &vm, int objectId, LoadedObjectSurface &surface, int targetWidth, int targetHeight) {
	const int objIndex = vm.getObjectIndex(objectId);
	if (objIndex < 0)
		return false;

	ObjectData &od = vm._objs[objIndex];
	if (od.obj_nr != objectId)
		return false;

	int objectHeight = od.height;
	if (vm._game.version < 7)
		objectHeight &= ~7;

	const int objectWidth = od.width;
	if (objectWidth <= 0 || objectHeight <= 0)
		return false;

	const byte *objectImage = vm.getObjectImage(vm.getOBIMFromObjectData(od), vm.getState(od.obj_nr));
	if (!objectImage)
		return false;

	Graphics::ManagedSurface indexed;
	indexed.create(objectWidth, objectHeight, Graphics::PixelFormat::createFormatCLUT8());
	memset(indexed.getPixels(), 0, indexed.pitch * indexed.h);

	VirtScreen tempVs;
	tempVs.clear();
	tempVs.init(objectWidth, objectHeight, indexed.pitch, indexed.getPixels(), Graphics::PixelFormat::createFormatCLUT8());
	tempVs.number = kMainVirtScreen;
	tempVs.topline = 0;
	tempVs.xstart = 0;
	tempVs.hasTwoBuffers = false;
	tempVs.backBuf = nullptr;
	tempVs.setDirtyRange(0, objectHeight);

	byte flags = od.flags | Gdi::dbObjectMode;
	if ((vm._game.id == GID_SAMNMAX && vm.getClass(od.obj_nr, kObjectClassIgnoreBoxes)) ||
		(vm._game.id == GID_FT && vm.getClass(od.obj_nr, kObjectClassPlayer)))
		flags |= Gdi::dbDrawMaskOnAll;

	vm._gdi->disableZBuffer();
	vm._gdi->drawBitmap(objectImage, &tempVs, 0, 0, objectWidth, objectHeight, 0, objectWidth / 8, flags);
	vm._gdi->enableZBuffer();

	Graphics::Palette activePalette(vm._currentPalette, Graphics::PALETTE_COUNT);
	Graphics::ManagedSurface rgba;
	convertSurfaceToRGBA32(indexed, &activePalette, rgba, true, 0);

	const Graphics::ManagedSurface *rgbaSource = &rgba;
	Graphics::ManagedSurface *scaled = nullptr;
	if (targetWidth > 0 && targetHeight > 0 && (rgba.w != targetWidth || rgba.h != targetHeight)) {
		scaled = rgba.scale(targetWidth, targetHeight, false);
		if (!scaled)
			return false;
		rgbaSource = scaled;
	}

	surface.surface.copyFrom(*rgbaSource);
	surface.hasAlpha = surfaceHasAlpha(surface.surface);
	delete scaled;
	return true;
}

bool MonkeyHdRenderer::loadRoomAssets(ScummEngine &vm, int roomResource) {
	clearRoomCache();

	const int backgroundId = resolveBackgroundId(vm, roomResource, _backgroundPaths);
	const int scale = vm.getDisplayScaleFactor();
	const int targetWidth = MAX(1, vm._roomWidth * scale);
	const int targetHeight = MAX(1, vm._roomHeight * scale);

	bool loaded = false;
	if (_backgroundPaths.contains(backgroundId)) {
		Common::FSNode backgroundNode(_backgroundPaths.getVal(backgroundId));
		warning("Monkey HD 4X: loading background id %d (room resource %d, current room %d) from '%s'", backgroundId, roomResource,
			vm._currentRoom, backgroundNode.getPath().toString(Common::Path::kNativeSeparator).c_str());
		loaded = backgroundNode.exists() && loadBackgroundPngRemapped(backgroundNode, _roomBackground, targetWidth, targetHeight);
		if (!loaded)
			warning("Monkey HD 4X: failed to load background '%s' for background id %d", backgroundNode.getPath().toString(Common::Path::kNativeSeparator).c_str(), backgroundId);
	}

	if (!loaded) {
		warning("Monkey HD 4X: falling back to original room background for room %d", roomResource);
		loaded = loadBackgroundFromGame(vm, _roomBackground, targetWidth, targetHeight);
	}
	if (!loaded)
		return false;

	_loadedRoom = roomResource;
	return true;
}

bool MonkeyHdRenderer::ensureRoomAssets(ScummEngine &vm, int roomResource) {
	if (_loadedRoom != roomResource)
		return loadRoomAssets(vm, roomResource);

	return _roomBackground.w != 0;
}

MonkeyHdRenderer::LoadedObjectSurface *MonkeyHdRenderer::getObjectSurface(ScummEngine &vm, int roomResource, int objectId) {
	const Common::String key = makeObjectKey(roomResource, objectId);
	if (_roomObjects.contains(key))
		return _roomObjects.getVal(key);

	int targetWidth = 0;
	int targetHeight = 0;
	if (roomResource != 99) {
		const int objectIndex = vm.getObjectIndex(objectId);
		if (objectIndex >= 0) {
			ObjectData &od = vm._objs[objectIndex];
			int objectHeight = od.height;
			if (vm._game.version < 7)
				objectHeight &= ~7;

			const int scale = vm.getDisplayScaleFactor();
			targetWidth = MAX(1, od.width * scale);
			targetHeight = MAX(1, objectHeight * scale);
		}
	}

	LoadedObjectSurface *surface = new LoadedObjectSurface();
	bool loaded = false;
	if (_objectPaths.contains(key)) {
		Common::FSNode objectNode(_objectPaths.getVal(key));
		loaded = objectNode.exists() && loadObjectPngRemapped(objectNode, *surface, targetWidth, targetHeight);
		if (!loaded) {
			warning("Monkey HD 4X: failed to load object '%s' for room %d object %d",
				objectNode.getPath().toString(Common::Path::kNativeSeparator).c_str(), roomResource, objectId);
		}
	}

	if (!loaded && roomResource != 99)
		loaded = loadObjectFromGame(vm, objectId, *surface, targetWidth, targetHeight);

	if (!loaded) {
		delete surface;
		return nullptr;
	}

	_roomObjects[key] = surface;
	return surface;
}

bool MonkeyHdRenderer::overlayInventoryVerbs(ScummEngine &vm, Graphics::Surface &dst, int patchX, int patchY, int patchWidth, int patchHeight) {
	if (!isReady() || dst.format.bytesPerPixel != 4 || patchWidth <= 0 || patchHeight <= 0)
		return false;

	const int scale = vm.getDisplayScaleFactor();
	const Common::Rect patchRect(patchX, patchY, patchX + patchWidth, patchY + patchHeight);
	const Common::Rect patchRectScaled(patchX * scale, patchY * scale, (patchX + patchWidth) * scale, (patchY + patchHeight) * scale);
	bool drewAny = false;

	for (InventoryVerbMap::const_iterator it = _inventoryVerbs.begin(); it != _inventoryVerbs.end(); ++it) {
		const InventoryVerbState &state = it->_value;
		if (!state.visible || state.objectId <= 0 || state.rect.isEmpty())
			continue;
		if (!state.rect.intersects(patchRect))
			continue;

		LoadedObjectSurface *surface = getObjectSurface(vm, 99, state.objectId);
		if (!surface || surface->surface.w == 0 || surface->surface.h == 0)
			continue;

		const Graphics::ManagedSurface *srcSurface = &surface->surface;
		Graphics::ManagedSurface *scaledSurface = nullptr;
		Common::Rect boxRect(state.rect.left * scale, state.rect.top * scale, state.rect.right * scale, state.rect.bottom * scale);
		Common::Rect targetRect = boxRect;

		if (surface->surface.w != boxRect.width() || surface->surface.h != boxRect.height()) {
			const int srcW = MAX<int>(1, surface->surface.w);
			const int srcH = MAX<int>(1, surface->surface.h);
			const int boxW = boxRect.width();
			const int boxH = boxRect.height();

			int fittedW = boxW;
			int fittedH = MAX(1, int((int64)srcH * fittedW / srcW));
			if (fittedH > boxH) {
				fittedH = boxH;
				fittedW = MAX(1, int((int64)srcW * fittedH / srcH));
			}

			targetRect.left += (boxW - fittedW) / 2;
			targetRect.top += (boxH - fittedH) / 2;
			targetRect.right = targetRect.left + fittedW;
			targetRect.bottom = targetRect.top + fittedH;

			const bool useFiltering = (srcW > fittedW || srcH > fittedH);
			scaledSurface = surface->surface.scale(fittedW, fittedH, useFiltering);
			if (!scaledSurface)
				continue;

			srcSurface = scaledSurface;
		}

		Common::Rect visibleRect = targetRect;
		visibleRect.clip(patchRectScaled);
		if (visibleRect.isEmpty()) {
			delete scaledSurface;
			continue;
		}

		Common::Rect srcRect(0, 0, srcSurface->w, srcSurface->h);
		if (visibleRect != targetRect) {
			const int64 targetW = MAX<int>(1, targetRect.width());
			const int64 targetH = MAX<int>(1, targetRect.height());
			const int64 srcW = srcSurface->w;
			const int64 srcH = srcSurface->h;

			srcRect.left = (int)(((int64)(visibleRect.left - targetRect.left) * srcW) / targetW);
			srcRect.top = (int)(((int64)(visibleRect.top - targetRect.top) * srcH) / targetH);
			srcRect.right = (int)((((int64)(visibleRect.right - targetRect.left) * srcW) + targetW - 1) / targetW);
			srcRect.bottom = (int)((((int64)(visibleRect.bottom - targetRect.top) * srcH) + targetH - 1) / targetH);
			srcRect.clip(Common::Rect(0, 0, srcSurface->w, srcSurface->h));
			if (srcRect.isEmpty()) {
				delete scaledSurface;
				continue;
			}
		}

		blitRGBAClipped(*srcSurface, srcRect, dst,
			Common::Point(visibleRect.left - patchRectScaled.left, visibleRect.top - patchRectScaled.top), surface->hasAlpha);
		delete scaledSurface;
		drewAny = true;
	}

	return drewAny;
}

bool MonkeyHdRenderer::renderStageRect(ScummEngine &vm, VirtScreen *vs, int x, int top, int width, int height, byte *dst, int dstPitch) {
	if (!isReady() || vs->number != kMainVirtScreen || width <= 0 || height <= 0)
		return false;

	const int scale = vm.getDisplayScaleFactor();
	const int roomResource = vm._roomResource > 0 ? vm._roomResource : vm._currentRoom;
	if (!ensureRoomAssets(vm, roomResource))
		return false;

	const bool rgbaOutput = (vm._outputPixelFormat.bytesPerPixel == 4);
	Graphics::Palette activePalette(vm._currentPalette, Graphics::PALETTE_COUNT);
	Common::HashMap<uint32, byte> colorCache;
	uint32 outputPalette[256];
	if (rgbaOutput)
		buildOutputPalette(vm._currentPalette, vm._outputPixelFormat, outputPalette);

	Graphics::Surface out;
	out.init(width * scale, height * scale, dstPitch, dst, rgbaOutput ? vm._outputPixelFormat : Graphics::PixelFormat::createFormatCLUT8());
	memset(dst, 0, dstPitch * out.h);

	const int worldX = vs->xstart + x;
	const int worldY = top;
	const Common::Rect backgroundRect(worldX * scale, worldY * scale, (worldX + width) * scale, (worldY + height) * scale);
	if (rgbaOutput) {
		if (_roomBackground.format.bytesPerPixel == 4)
			blitRGBAClipped(_roomBackground, backgroundRect, out, Common::Point(0, 0), _roomBackgroundHasAlpha);
		else
			blitOpaqueMappedClipped(_roomBackground, backgroundRect, out, Common::Point(0, 0), outputPalette);
	} else {
		if (_roomBackground.format.bytesPerPixel == 4)
			blitRGBAToCLUT8Clipped(_roomBackground, backgroundRect, out, Common::Point(0, 0), activePalette, colorCache, _roomBackgroundHasAlpha);
		else
			blitOpaqueClipped(_roomBackground, backgroundRect, out, Common::Point(0, 0));
	}

	if (!_roomBackgroundFromGame) {
		const int mask = (vm._game.version <= 2) ? kObjectStateIntrinsic : 0xF;
		for (int i = vm._numLocalObjects - 1; i > 0; --i) {
			ObjectData &od = vm._objs[i];
			if (od.obj_nr <= 0 || !(od.state & mask))
				continue;

			LoadedObjectSurface *surface = getObjectSurface(vm, roomResource, od.obj_nr);
			if (!surface || surface->surface.w == 0 || surface->surface.h == 0)
				continue;

			const int objLeft = od.x_pos * scale;
			const int objTop = od.y_pos * scale;
			const Common::Rect objectRect(objLeft, objTop, objLeft + surface->surface.w, objTop + surface->surface.h);
			const Common::Rect viewRect(worldX * scale, worldY * scale, (worldX + width) * scale, (worldY + height) * scale);
			if (!objectRect.intersects(viewRect))
				continue;

			Common::Rect clipped = objectRect;
			clipped.clip(viewRect);
			const Common::Rect srcRect(clipped.left - objectRect.left, clipped.top - objectRect.top,
				clipped.right - objectRect.left, clipped.bottom - objectRect.top);
			const Common::Point dstPos(clipped.left - viewRect.left, clipped.top - viewRect.top);
			if (rgbaOutput) {
				if (surface->surface.format.bytesPerPixel == 4)
					blitRGBAClipped(surface->surface, srcRect, out, dstPos, surface->hasAlpha);
				else if (surface->hasAlpha)
					blitKeyedMappedClipped(surface->surface, srcRect, out, dstPos, 0, outputPalette);
				else
					blitOpaqueMappedClipped(surface->surface, srcRect, out, dstPos, outputPalette);
			} else if (surface->surface.format.bytesPerPixel == 4) {
				blitRGBAToCLUT8Clipped(surface->surface, srcRect, out, dstPos, activePalette, colorCache, surface->hasAlpha);
			} else if (surface->hasAlpha) {
				blitKeyedClipped(surface->surface, srcRect, out, dstPos, 0);
			} else {
				blitOpaqueClipped(surface->surface, srcRect, out, dstPos);
			}
		}
	}

	const byte *frontBase = (const byte *)vs->getPixels(0, 0);
	const byte *backBase = (const byte *)vs->getBackPixels(0, 0);
	overlayScaledBitmapDiff(vm._currentPalette, out, frontBase, backBase, vs->pitch, vs->w, vs->h, x, top, width, height, scale);

	const int overlayY = vs->topline + top - vm._screenTop;
	const int textScale = MAX(1, vm._textSurfaceMultiplier);
	if (scale == textScale) {
		for (int yy = 0; yy < height * textScale; ++yy) {
			const byte *textRow = (const byte *)vm._textSurface.getBasePtr(x * textScale, overlayY * textScale + yy);
			if (rgbaOutput) {
				uint32 *dstRow = (uint32 *)out.getBasePtr(0, yy);
				for (int xx = 0; xx < width * textScale; ++xx) {
					if (textRow[xx] != CHARSET_MASK_TRANSPARENCY)
						dstRow[xx] = outputPalette[textRow[xx]];
				}
			} else {
				byte *dstRow = (byte *)out.getBasePtr(0, yy);
				for (int xx = 0; xx < width * textScale; ++xx) {
					if (textRow[xx] != CHARSET_MASK_TRANSPARENCY)
						dstRow[xx] = textRow[xx];
				}
			}
		}
		} else if (scale == textScale * 2) {
			const int srcWidth = width * textScale;
			const int srcHeight = height * textScale;
		const int scaledWidth = width * scale;
		const int scaledHeight = height * scale;
		Common::Array<byte> scaledText(scaledWidth * scaledHeight, (byte)CHARSET_MASK_TRANSPARENCY);
		const byte *textBase = (const byte *)vm._textSurface.getBasePtr(x * textScale, overlayY * textScale);
		scale2xBitmap8(scaledText.data(), scaledWidth, textBase, vm._textSurface.pitch, srcWidth, srcHeight);
		for (int yy = 0; yy < scaledHeight; ++yy) {
			const byte *textRow = scaledText.data() + yy * scaledWidth;
			if (rgbaOutput) {
				uint32 *dstRow = (uint32 *)out.getBasePtr(0, yy);
				for (int xx = 0; xx < scaledWidth; ++xx) {
					if (textRow[xx] != CHARSET_MASK_TRANSPARENCY)
						dstRow[xx] = outputPalette[textRow[xx]];
				}
			} else {
				byte *dstRow = (byte *)out.getBasePtr(0, yy);
				for (int xx = 0; xx < scaledWidth; ++xx) {
					if (textRow[xx] != CHARSET_MASK_TRANSPARENCY)
						dstRow[xx] = textRow[xx];
				}
			}
		}
	} else if (scale == textScale * 4) {
		const int srcWidth = width * textScale;
		const int srcHeight = height * textScale;
		const int scaledWidth = width * scale;
		const int scaledHeight = height * scale;
		Common::Array<byte> scaledText(scaledWidth * scaledHeight, (byte)CHARSET_MASK_TRANSPARENCY);
		const byte *textBase = (const byte *)vm._textSurface.getBasePtr(x * textScale, overlayY * textScale);
		advMameScale4xBitmap8(scaledText.data(), scaledWidth, textBase, vm._textSurface.pitch, srcWidth, srcHeight);
		for (int yy = 0; yy < scaledHeight; ++yy) {
			const byte *textRow = scaledText.data() + yy * scaledWidth;
			if (rgbaOutput) {
				uint32 *dstRow = (uint32 *)out.getBasePtr(0, yy);
				for (int xx = 0; xx < scaledWidth; ++xx) {
					if (textRow[xx] != CHARSET_MASK_TRANSPARENCY)
						dstRow[xx] = outputPalette[textRow[xx]];
				}
			} else {
				byte *dstRow = (byte *)out.getBasePtr(0, yy);
				for (int xx = 0; xx < scaledWidth; ++xx) {
					if (textRow[xx] != CHARSET_MASK_TRANSPARENCY)
						dstRow[xx] = textRow[xx];
				}
			}
		}
	} else {
		const int textBlitScale = (scale % textScale == 0) ? (scale / textScale) : 1;
		for (int yy = 0; yy < height * textScale; ++yy) {
			const byte *textRow = (const byte *)vm._textSurface.getBasePtr(x * textScale, overlayY * textScale + yy);
			if (rgbaOutput) {
				for (int xx = 0; xx < width * textScale; ++xx) {
					if (textRow[xx] == CHARSET_MASK_TRANSPARENCY)
						continue;
					for (int sy = 0; sy < textBlitScale; ++sy) {
						uint32 *dstRow = (uint32 *)out.getBasePtr(0, yy * textBlitScale + sy);
						for (int sx = 0; sx < textBlitScale; ++sx)
							dstRow[xx * textBlitScale + sx] = outputPalette[textRow[xx]];
					}
				}
			} else {
				byte *dstRow = (byte *)out.getBasePtr(0, yy * textBlitScale);
				for (int xx = 0; xx < width * textScale; ++xx) {
					if (textRow[xx] != CHARSET_MASK_TRANSPARENCY)
						plotScaledPixel(textRow[xx], dstRow + xx * textBlitScale, dstPitch, textBlitScale);
				}
			}
		}
	}

	return true;
}

} // End of namespace Scumm
