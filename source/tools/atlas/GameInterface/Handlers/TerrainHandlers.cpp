/* Copyright (C) 2010 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "precompiled.h"

#include "MessageHandler.h"

#include "../CommandProc.h"

#include "graphics/Patch.h"
#include "graphics/TerrainTextureManager.h"
#include "graphics/TerrainTextureEntry.h"
#include "graphics/Terrain.h"
#include "ps/Game.h"
#include "ps/World.h"
#include "lib/ogl.h"
#include "lib/res/graphics/ogl_tex.h"
#include "simulation2/Simulation2.h"
#include "simulation2/components/ICmpPathfinder.h"
#include "simulation2/components/ICmpTerrain.h"

#include "../Brushes.h"
#include "../DeltaArray.h"
#include "../View.h"

namespace AtlasMessage {

QUERYHANDLER(GetTerrainGroups)
{
	const CTerrainTextureManager::TerrainGroupMap &groups = g_TexMan.GetGroups();
	std::vector<std::wstring> groupnames;
	for (CTerrainTextureManager::TerrainGroupMap::const_iterator it = groups.begin(); it != groups.end(); ++it)
		groupnames.push_back(CStrW(it->first));
	msg->groupnames = groupnames;
}

static bool CompareTerrain(const sTerrainGroupPreview& a, const sTerrainGroupPreview& b) 
{
	return (wcscmp(a.name.c_str(), b.name.c_str()) < 0);
}

QUERYHANDLER(GetTerrainGroupPreviews)
{
	std::vector<sTerrainGroupPreview> previews;

	CTerrainGroup* group = g_TexMan.FindGroup(CStrW(*msg->groupname));
	for (std::vector<CTerrainTextureEntry*>::const_iterator it = group->GetTerrains().begin(); it != group->GetTerrains().end(); ++it)
	{
		previews.push_back(sTerrainGroupPreview());
		previews.back().name = CStrW((*it)->GetTag());

		std::vector<unsigned char> buf (msg->imagewidth*msg->imageheight*3);

		// It's not good to shrink the entire texture to fit the small preview
		// window, since it's the fine details in the texture that are
		// interesting; so just go down one mipmap level, then crop a chunk
		// out of the middle.

		// Read the size of the texture. (Usually loads the texture from
		// disk, which is slow.)
		GLint w, h;
		ssize_t level = 1; // level 0 is the original size
		(*it)->GetTexture()->Bind();
		glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH,  &w);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &h);

		if (w < msg->imagewidth || h < msg->imageheight)
		{
			// Oops, too small to preview - just use a flat colour
			u32 c = (*it)->GetBaseColor();
			for (ssize_t i = 0; i < msg->imagewidth*msg->imageheight; ++i)
			{
				buf[i*3+0] = (c>>16) & 0xff;
				buf[i*3+1] = (c>>8) & 0xff;
				buf[i*3+2] = (c>>0) & 0xff;
			}
		}
		else
		{
			// Read the whole texture into a new buffer
			unsigned char* texdata = new unsigned char[w*h*3];
			glGetTexImage(GL_TEXTURE_2D, level, GL_RGB, GL_UNSIGNED_BYTE, texdata);

			// Extract the middle section (as a representative preview),
			// and copy into buf
			unsigned char* texdata_ptr = texdata + (w*(h - msg->imageheight)/2 + (w - msg->imagewidth)/2) * 3;
			unsigned char* buf_ptr = &buf[0];
			for (ssize_t y = 0; y < msg->imageheight; ++y)
			{
				memcpy(buf_ptr, texdata_ptr, msg->imagewidth*3);
				buf_ptr += msg->imagewidth*3;
				texdata_ptr += w*3;
			}

			delete[] texdata;
		}

		previews.back().loaded = (*it)->GetTexture()->IsLoaded();
		previews.back().imagewidth = msg->imagewidth;
		previews.back().imageheight = msg->imageheight;
		previews.back().imagedata = buf;
	}

	// Sort the list alphabetically by name
	std::sort(previews.begin(), previews.end(), CompareTerrain);
	msg->previews = previews;
}

QUERYHANDLER(GetTerrainPassabilityClasses)
{
	CmpPtr<ICmpPathfinder> cmpPathfinder(*View::GetView_Game()->GetSimulation2(), SYSTEM_ENTITY);
	if (!cmpPathfinder.null())
	{
		std::vector<std::string> names = cmpPathfinder->GetPassabilityClasses();

		std::vector<std::wstring> classnames;
		for (std::vector<std::string>::iterator it = names.begin(); it != names.end(); ++it)
			classnames.push_back(CStrW(*it));
		msg->classnames = classnames;
	}
}

//////////////////////////////////////////////////////////////////////////

BEGIN_COMMAND(PaintTerrain)
{
	struct TerrainTile
	{
		TerrainTile(CTerrainTextureEntry* t, ssize_t p) : tex(t), priority(p) {}
		CTerrainTextureEntry* tex;
		ssize_t priority;
	};
	class TerrainArray : public DeltaArray2D<TerrainTile>
	{
	public:
		void Init()
		{
			m_Terrain = g_Game->GetWorld()->GetTerrain();
			m_VertsPerSide = g_Game->GetWorld()->GetTerrain()->GetVerticesPerSide();
		}

		void PaintTile(ssize_t x, ssize_t y, CTerrainTextureEntry* tex, ssize_t priority)
		{
			// Ignore out-of-bounds tiles
			if (size_t(x) >= size_t(m_VertsPerSide-1) || size_t(y) >= size_t(m_VertsPerSide-1))
				return;

			// Priority system: If the new tile should have a high priority,
			// set it to one plus the maximum priority of all surrounding tiles
			// (so that it's definitely the highest).
			// Similar for low priority.
			ssize_t greatest = 0;
			ssize_t scale = (priority == ePaintTerrainPriority::HIGH ? +1 : -1);
			CMiniPatch* tile;
#define TILE(dx, dy) tile = m_Terrain->GetTile(x dx, y dy); if (tile && tile->GetPriority()*scale > greatest) greatest = tile->GetPriority()*scale;
			TILE(-1, -1) TILE(+0, -1) TILE(+1, -1)
			TILE(-1, +0)              TILE(+1, +0)
			TILE(-1, +1) TILE(+0, +1) TILE(+1, +1)
#undef TILE
			set(x,y, TerrainTile(tex, (greatest+1)*scale));
		}

	protected:
		TerrainTile getOld(ssize_t x, ssize_t y)
		{
			CMiniPatch* mp = m_Terrain->GetTile(x, y);
			debug_assert(mp);
			return TerrainTile(mp->Tex, mp->Priority);
		}
		void setNew(ssize_t x, ssize_t y, const TerrainTile& val)
		{
			CMiniPatch* mp = m_Terrain->GetTile(x, y);
			debug_assert(mp);
			mp->Tex = val.tex;
			mp->Priority = val.priority;
		}

		CTerrain* m_Terrain;
		ssize_t m_VertsPerSide;
	};

	TerrainArray m_TerrainDelta;
	ssize_t m_i0, m_j0, m_i1, m_j1;

	cPaintTerrain()
	{
		m_TerrainDelta.Init();
	}

	void MakeDirty()
	{
		g_Game->GetWorld()->GetTerrain()->MakeDirty(m_i0, m_j0, m_i1, m_j1, RENDERDATA_UPDATE_INDICES);
		CmpPtr<ICmpTerrain> cmpTerrain(*g_Game->GetSimulation2(), SYSTEM_ENTITY);
		if (!cmpTerrain.null())
			cmpTerrain->MakeDirty(m_i0, m_j0, m_i1, m_j1);
	}

	void Do()
	{
		g_CurrentBrush.m_Centre = msg->pos->GetWorldSpace();

		ssize_t x0, y0;
		g_CurrentBrush.GetBottomLeft(x0, y0);

		CTerrainTextureEntry* texentry = g_TexMan.FindTexture(CStrW(*msg->texture));
		if (! texentry)
		{
			debug_warn(L"Can't find texentry"); // TODO: nicer error handling
			return;
		}

		for (ssize_t dy = 0; dy < g_CurrentBrush.m_H; ++dy)
		{
			for (ssize_t dx = 0; dx < g_CurrentBrush.m_W; ++dx)
			{
				if (g_CurrentBrush.Get(dx, dy) > 0.5f) // TODO: proper solid brushes
					m_TerrainDelta.PaintTile(x0+dx, y0+dy, texentry, msg->priority);
			}
		}

		m_i0 = x0;
		m_j0 = y0;
		m_i1 = x0 + g_CurrentBrush.m_W;
		m_j1 = y0 + g_CurrentBrush.m_H;
		MakeDirty();
	}

	void Undo()
	{
		m_TerrainDelta.Undo();
		MakeDirty();
	}

	void Redo()
	{
		m_TerrainDelta.Redo();
		MakeDirty();
	}

	void MergeIntoPrevious(cPaintTerrain* prev)
	{
		prev->m_TerrainDelta.OverlayWith(m_TerrainDelta);
		prev->m_i0 = std::min(prev->m_i0, m_i0);
		prev->m_j0 = std::min(prev->m_j0, m_j0);
		prev->m_i1 = std::max(prev->m_i1, m_i1);
		prev->m_j1 = std::max(prev->m_j1, m_j1);
	}
};
END_COMMAND(PaintTerrain)


}
