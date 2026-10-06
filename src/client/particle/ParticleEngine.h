#ifndef NET_MINECRAFT_CLIENT_PARTICLE__ParticleEngine_H__
#define NET_MINECRAFT_CLIENT_PARTICLE__ParticleEngine_H__

//package net.minecraft.client.particle;

#include <vector>
#include "../../world/entity/Entity.h"
#include "../../world/level/tile/Tile.h"
#include "../renderer/gles.h"

class Textures;
class Level;
class Particle;
typedef std::vector<Particle*> ParticleList;

class ParticleEngine
{
public:
    static const int MISC_TEXTURE = 0;
    static const int TERRAIN_TEXTURE = 1;
    static const int ITEM_TEXTURE = 2;
    static const int ENTITY_PARTICLE_TEXTURE = 3;

    static const int TEXTURE_COUNT = 4;

	/*
	 * Cap on live particles per texture bucket, enforced in add().
	 * Declared here so the tests can read the same value instead of hard-coding it.
	 */
	static const int MAX_PARTICLES_PER_TEXTURE = 200;

    ParticleEngine(Level* level, Textures* textures);
	~ParticleEngine();

    void add(Particle* p);
	void destroy(int x, int y, int z);

    void tick();
    void render(Entity* player, float a);
    void renderLit(Entity* player, float a);

	void setLevel(Level* level);

	void crack(int x, int y, int z, int face);

	std::string countParticles();

	// Live particle total, used by the soak test to spot unbounded growth.
	int totalCount()
	{
		int n = 0;
		for (int i = 0; i < 4; ++i)
			n += (int)particles[i].size();
		return n;
	}

	/*
	 * Live count for one texture bucket. The cap is per bucket, so the total
	 * alone cannot tell whether every bucket is limited; this can.
	 */
	int bucketCount(int i) const
	{
		if (i < 0 || i >= TEXTURE_COUNT) return -1;
		return (int)particles[i].size();
	}

	static const int perBucketLimit() { return MAX_PARTICLES_PER_TEXTURE; }

	// Drop every live particle. Public so tests can start from a known state; the
	// engine already calls clear() from its destructor.
	void clearAll() { clear(); }

protected:
	void clear();

	Level* level;
	GLuint textureIds[4];

private:
    ParticleList particles[4];
    Textures* textures;
    Random random;
};

#endif /*NET_MINECRAFT_CLIENT_PARTICLE__ParticleEngine_H__*/
