#ifndef SCUMM_MONKEY_HD_H
#define SCUMM_MONKEY_HD_H

#include "common/fs.h"
#include "common/hashmap.h"
#include "common/path.h"
#include "common/rect.h"
#include "common/str.h"
#include "graphics/managed_surface.h"
#include "graphics/palette.h"

namespace Scumm {

class ScummEngine;
struct VirtScreen;

class MonkeyHdRenderer {
public:
	MonkeyHdRenderer();
	~MonkeyHdRenderer();

	bool init(const Common::FSNode &root);
	bool isReady() const;
	bool renderStageRect(ScummEngine &vm, VirtScreen *vs, int x, int top, int width, int height, byte *dst, int dstPitch);
	bool overlayInventoryVerbs(ScummEngine &vm, Graphics::Surface &dst, int patchX, int patchY, int patchWidth, int patchHeight);
	void rememberVerbObject(int verbSlot, int roomResource, int objectId);
	void forgetVerbObject(int verbSlot);
	bool isInventoryVerb(int verbSlot, int objectId) const;
	bool activateInventoryVerb(int verbSlot, int objectId, const Common::Rect &screenRect);
	void deactivateInventoryVerb(int verbSlot);

private:
	struct LoadedObjectSurface {
		Graphics::ManagedSurface surface;
		bool hasAlpha = false;
	};

	struct InventoryVerbState {
		int objectId = 0;
		Common::Rect rect;
		bool visible = false;
	};

	typedef Common::HashMap<int, Common::Path> RoomPathMap;
	typedef Common::HashMap<Common::String, Common::Path> ObjectPathMap;
	typedef Common::HashMap<Common::String, LoadedObjectSurface *> ObjectSurfaceMap;
	typedef Common::HashMap<int, InventoryVerbState> InventoryVerbMap;

	Common::FSNode _backgroundsDir;
	Common::FSNode _objectsDir;
	RoomPathMap _backgroundPaths;
	RoomPathMap _baseBackgroundPaths;
	ObjectPathMap _objectPaths;
	ObjectPathMap _baseObjectPaths;
	ObjectSurfaceMap _roomObjects;
	InventoryVerbMap _inventoryVerbs;
	Graphics::ManagedSurface _roomBackground;
	bool _initialized = false;
	bool _roomBackgroundFromGame = false;
	bool _roomBackgroundHasAlpha = false;
	int _loadedRoom = -1;

	void clearRoomCache();
	bool indexRoot(const Common::FSNode &root);
	bool ensureRoomAssets(ScummEngine &vm, int roomResource);
	bool loadRoomAssets(ScummEngine &vm, int roomResource);
	int resolveBackgroundId(const ScummEngine &vm, int roomResource, const RoomPathMap &paths) const;
	bool loadBackgroundPngRemapped(const Common::FSNode &node, Graphics::ManagedSurface &surface, int targetWidth, int targetHeight);
	bool loadObjectPngRemapped(const Common::FSNode &node, LoadedObjectSurface &surface, int targetWidth, int targetHeight);
	bool loadBackgroundFromGame(ScummEngine &vm, Graphics::ManagedSurface &surface, int targetWidth, int targetHeight);
	bool loadObjectFromGame(ScummEngine &vm, int objectId, LoadedObjectSurface &surface, int targetWidth, int targetHeight);
	LoadedObjectSurface *getObjectSurface(ScummEngine &vm, int roomResource, int objectId);
	Common::String makeObjectKey(int room, int objectId) const;
};

} // End of namespace Scumm

#endif
