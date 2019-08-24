/* Copyright (C) 2019 Wildfire Games.
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

#include "COList.h"

#include "gui/CGUIColor.h"
#include "i18n/L10n.h"
#include "ps/CLogger.h"

const float SORT_SPRITE_DIM = 16.0f;
const CPos COLUMN_SHIFT = CPos(0, 4);

COList::COList(CGUI& pGUI)
	: CList(pGUI), IGUIObject(pGUI)
{
	AddSetting<CGUISpriteInstance>("sprite_heading");
	AddSetting<bool>("sortable"); // The actual sorting is done in JS for more versatility
	AddSetting<CStr>("selected_column");
	AddSetting<int>("selected_column_order");
	AddSetting<CGUISpriteInstance>("sprite_asc");  // Show the order of sorting
	AddSetting<CGUISpriteInstance>("sprite_desc");
	AddSetting<CGUISpriteInstance>("sprite_not_sorted");
}

void COList::SetupText()
{
	const CGUIList& pList = GUI<CGUIList>::GetSetting(this, "list");
	m_ItemsYPositions.resize(pList.m_Items.size() + 1);

	// Delete all generated texts. Some could probably be saved,
	//  but this is easier, and this function will never be called
	//  continuously, or even often, so it'll probably be okay.
	m_GeneratedTexts.clear();

	const CStrW& font = GUI<CStrW>::GetSetting(this, "font");
	const bool scrollbar = GUI<bool>::GetSetting(this, "scrollbar");

	m_TotalAvailableColumnWidth = GetListRect().GetWidth();
	// remove scrollbar if applicable
	if (scrollbar && GetScrollBar(0).GetStyle())
		m_TotalAvailableColumnWidth -= GetScrollBar(0).GetStyle()->m_Width;

	const float buffer_zone = GUI<float>::GetSetting(this, "buffer_zone");

	m_HeadingHeight = SORT_SPRITE_DIM; // At least the size of the sorting sprite

	for (const COListColumn& column : m_Columns)
	{
		float width = column.m_Width;
		if (column.m_Width > 0 && column.m_Width < 1)
			width *= m_TotalAvailableColumnWidth;

		CGUIString gui_string;
		gui_string.SetValue(column.m_Heading);

		const CGUIText& text = AddText(gui_string, font, width, buffer_zone, this);
		m_HeadingHeight = std::max(m_HeadingHeight, text.GetSize().cy + COLUMN_SHIFT.y);
	}

	// Generate texts
	float buffered_y = 0.f;

	for (size_t i = 0; i < pList.m_Items.size(); ++i)
	{
		m_ItemsYPositions[i] = buffered_y;
		float shift = 0.0f;
		for (const COListColumn& column : m_Columns)
		{
			float width = column.m_Width;
			if (column.m_Width > 0 && column.m_Width < 1)
				width *= m_TotalAvailableColumnWidth;

			CGUIList& pList_c = GUI<CGUIList>::GetSetting(this, "list_" + column.m_Id);
			CGUIText* text;
			if (!pList_c.m_Items[i].GetOriginalString().empty())
				text = &AddText(pList_c.m_Items[i], font, width, buffer_zone, this);
			else
			{
				// Minimum height of a space character of the current font size
				CGUIString align_string;
				align_string.SetValue(L" ");
				text = &AddText(align_string, font, width, buffer_zone, this);
			}
			shift = std::max(shift, text->GetSize().cy);
		}
		buffered_y += shift;
	}

	m_ItemsYPositions[pList.m_Items.size()] = buffered_y;

	if (scrollbar)
	{
		CRect rect = GetListRect();
		GetScrollBar(0).SetScrollRange(m_ItemsYPositions.back());
		GetScrollBar(0).SetScrollSpace(rect.GetHeight());

		GetScrollBar(0).SetX(rect.right);
		GetScrollBar(0).SetY(rect.top);
		GetScrollBar(0).SetZ(GetBufferedZ());
		GetScrollBar(0).SetLength(rect.bottom - rect.top);
	}
}

CRect COList::GetListRect() const
{
	return m_CachedActualSize + CRect(0, m_HeadingHeight, 0, 0);
}

void COList::HandleMessage(SGUIMessage& Message)
{
	CList::HandleMessage(Message);

	switch (Message.type)
	{
	// If somebody clicks on the column heading
	case GUIM_MOUSE_PRESS_LEFT:
	{
		if (!GUI<bool>::GetSetting(this, "sortable"))
			return;

		const CPos& mouse = m_pGUI.GetMousePos();
		if (!m_CachedActualSize.PointInside(mouse))
			return;

		// Copies, so that these settings are only modfied via SetSettings later.
		CStr selectedColumn = GUI<CStr>::GetSetting(this, "selected_column");
		int selectedColumnOrder = GUI<int>::GetSetting(this, "selected_column_order");

		float xpos = 0;
		for (const COListColumn& column : m_Columns)
		{
			if (GUI<bool>::GetSetting(this, "hidden_" + column.m_Id))
				continue;

			float width = column.m_Width;
			// Check if it's a decimal value, and if so, assume relative positioning.
			if (column.m_Width < 1 && column.m_Width > 0)
				width *= m_TotalAvailableColumnWidth;
			CPos leftTopCorner = m_CachedActualSize.TopLeft() + CPos(xpos, 0);
			if (mouse.x >= leftTopCorner.x &&
				mouse.x < leftTopCorner.x + width &&
				mouse.y < leftTopCorner.y + m_HeadingHeight)
			{
				if (column.m_Id != selectedColumn)
				{
					selectedColumnOrder = 1;
					selectedColumn = column.m_Id;
				}
				else
					selectedColumnOrder = -selectedColumnOrder;

				GUI<CStr>::SetSetting(this, "selected_column", selectedColumn);
				GUI<int>::SetSetting(this, "selected_column_order", selectedColumnOrder);

				ScriptEvent("selectioncolumnchange");
				PlaySound("sound_selected");
				return;
			}
			xpos += width;
		}
		return;
	}
	default:
		return;
	}
}

bool COList::HandleAdditionalChildren(const XMBElement& child, CXeromyces* pFile)
{
	#define ELMT(x) int elmt_##x = pFile->GetElementID(#x)
	#define ATTR(x) int attr_##x = pFile->GetAttributeID(#x)
	ELMT(item);
	ELMT(column);
	ELMT(translatableAttribute);
	ATTR(id);
	ATTR(context);

	if (child.GetNodeName() == elmt_item)
	{
		AddItem(child.GetText().FromUTF8(), child.GetText().FromUTF8());
		return true;
	}
	else if (child.GetNodeName() == elmt_column)
	{
		COListColumn column;
		bool hidden = false;

		for (XMBAttribute attr : child.GetAttributes())
		{
			CStr attr_name(pFile->GetAttributeString(attr.Name));
			CStr attr_value(attr.Value);

			if (attr_name == "color")
			{
				if (!GUI<CGUIColor>::ParseString(&m_pGUI, attr_value.FromUTF8(), column.m_TextColor))
					LOGERROR("GUI: Error parsing '%s' (\"%s\")", attr_name.c_str(), attr_value.c_str());
			}
			else if (attr_name == "id")
			{
				column.m_Id = attr_value;
			}
			else if (attr_name == "hidden")
			{
				if (!GUI<bool>::ParseString(&m_pGUI, attr_value.FromUTF8(), hidden))
					LOGERROR("GUI: Error parsing '%s' (\"%s\")", attr_name.c_str(), attr_value.c_str());
			}
			else if (attr_name == "width")
			{
				float width;
				if (!GUI<float>::ParseString(&m_pGUI, attr_value.FromUTF8(), width))
					LOGERROR("GUI: Error parsing '%s' (\"%s\")", attr_name.c_str(), attr_value.c_str());
				else
				{
					// Check if it's a relative value, and save as decimal if so.
					if (attr_value.find("%") != std::string::npos)
						width = width / 100.f;
					column.m_Width = width;
				}
			}
			else if (attr_name == "heading")
			{
				column.m_Heading = attr_value.FromUTF8();
			}
		}

		for (XMBElement grandchild : child.GetChildNodes())
		{
			if (grandchild.GetNodeName() != elmt_translatableAttribute)
				continue;

			CStr attributeName(grandchild.GetAttributes().GetNamedItem(attr_id));
			// only the heading is translatable for list column
			if (attributeName.empty() || attributeName != "heading")
			{
				LOGERROR("GUI: translatable attribute in olist column that isn't a heading. (object: %s)", this->GetPresentableName().c_str());
				continue;
			}

			CStr value(grandchild.GetText());
			if (value.empty())
				continue;

			CStr context(grandchild.GetAttributes().GetNamedItem(attr_context)); // Read the context if any.
			if (!context.empty())
			{
				CStr translatedValue(g_L10n.TranslateWithContext(context, value));
				column.m_Heading = translatedValue.FromUTF8();
			}
			else
			{
				CStr translatedValue(g_L10n.Translate(value));
				column.m_Heading = translatedValue.FromUTF8();
			}
		}

		AddSetting<CGUIList>("list_" + column.m_Id);
		AddSetting<bool>("hidden_" + column.m_Id);
		GUI<bool>::SetSetting(this, "hidden_" + column.m_Id, hidden);

		m_Columns.emplace_back(std::move(column));

		SetupText();

		return true;
	}

	return false;
}

void COList::DrawList(const int& selected, const CStr& _sprite, const CStr& _sprite_selected, const CStr& _textcolor)
{
	const float bz = GetBufferedZ();
	const bool scrollbar = GUI<bool>::GetSetting(this, "scrollbar");

	if (scrollbar)
		IGUIScrollBarOwner::Draw();

	CRect rect = GetListRect();

	CGUISpriteInstance& sprite = GUI<CGUISpriteInstance>::GetSetting(this, _sprite);
	CGUISpriteInstance& sprite_selectarea = GUI<CGUISpriteInstance>::GetSetting(this, _sprite_selected);

	const int cell_id = GUI<int>::GetSetting(this, "cell_id");

	m_pGUI.DrawSprite(sprite, cell_id, bz, rect);

	float scroll = 0.f;
	if (scrollbar)
		scroll = GetScrollBar(0).GetPos();

	// Draw item selection
	if (selected != -1)
	{
		ENSURE(selected >= 0 && selected+1 < (int)m_ItemsYPositions.size());

		// Get rectangle of selection:
		CRect rect_sel(rect.left, rect.top + m_ItemsYPositions[selected] - scroll,
		               rect.right, rect.top + m_ItemsYPositions[selected+1] - scroll);

		if (rect_sel.top <= rect.bottom &&
			rect_sel.bottom >= rect.top)
		{
			if (rect_sel.bottom > rect.bottom)
				rect_sel.bottom = rect.bottom;
			if (rect_sel.top < rect.top)
				rect_sel.top = rect.top;

			if (scrollbar)
			{
				// Remove any overlapping area of the scrollbar.
				if (rect_sel.right > GetScrollBar(0).GetOuterRect().left &&
					rect_sel.right <= GetScrollBar(0).GetOuterRect().right)
					rect_sel.right = GetScrollBar(0).GetOuterRect().left;

				if (rect_sel.left >= GetScrollBar(0).GetOuterRect().left &&
					rect_sel.left < GetScrollBar(0).GetOuterRect().right)
					rect_sel.left = GetScrollBar(0).GetOuterRect().right;
			}

			// Draw item selection
			m_pGUI.DrawSprite(sprite_selectarea, cell_id, bz+0.05f, rect_sel);
		}
	}

	// Draw line above column header
	CGUISpriteInstance& sprite_heading = GUI<CGUISpriteInstance>::GetSetting(this, "sprite_heading");
	CRect rect_head(m_CachedActualSize.left, m_CachedActualSize.top, m_CachedActualSize.right,
									m_CachedActualSize.top + m_HeadingHeight);
	m_pGUI.DrawSprite(sprite_heading, cell_id, bz, rect_head);

	// Draw column headers
	const bool sortable = GUI<bool>::GetSetting(this, "sortable");
	const CStr& selectedColumn = GUI<CStr>::GetSetting(this, "selected_column");
	const int selectedColumnOrder = GUI<int>::GetSetting(this, "selected_column_order");
	const CGUIColor& color = GUI<CGUIColor>::GetSetting(this, _textcolor);

	float xpos = 0;
	for (size_t col = 0; col < m_Columns.size(); ++col)
	{
		if (GUI<bool>::GetSetting(this, "hidden_" + m_Columns[col].m_Id))
			continue;

		// Check if it's a decimal value, and if so, assume relative positioning.
		float width = m_Columns[col].m_Width;
		if (m_Columns[col].m_Width < 1 && m_Columns[col].m_Width > 0)
			width *= m_TotalAvailableColumnWidth;

		CPos leftTopCorner = m_CachedActualSize.TopLeft() + CPos(xpos, 0);

		// Draw sort arrows in colum header
		if (sortable)
		{
			CStr spriteName;
			if (selectedColumn == m_Columns[col].m_Id)
			{
				if (selectedColumnOrder == 0)
					LOGERROR("selected_column_order must not be 0");

				if (selectedColumnOrder != -1)
					spriteName = "sprite_asc";
				else
					spriteName = "sprite_desc";
			}
			else
				spriteName = "sprite_not_sorted";

			CGUISpriteInstance& sprite = GUI<CGUISpriteInstance>::GetSetting(this, spriteName);
			m_pGUI.DrawSprite(sprite, cell_id, bz + 0.1f, CRect(leftTopCorner + CPos(width - SORT_SPRITE_DIM, 0), leftTopCorner + CPos(width, SORT_SPRITE_DIM)));
		}

		// Draw column header text
		DrawText(col, color, leftTopCorner + COLUMN_SHIFT, bz + 0.1f, rect_head);
		xpos += width;
	}

	// Draw list items for each column
	const CGUIList& pList = GUI<CGUIList>::GetSetting(this, "list");
	const size_t objectsCount = m_Columns.size();
	for (size_t i = 0; i < pList.m_Items.size(); ++i)
	{
		if (m_ItemsYPositions[i+1] - scroll < 0 ||
			m_ItemsYPositions[i] - scroll > rect.GetHeight())
			continue;

		const float rowHeight = m_ItemsYPositions[i+1] - m_ItemsYPositions[i];

		// Clipping area (we'll have to substract the scrollbar)
		CRect cliparea = GetListRect();

		if (scrollbar)
		{
			if (cliparea.right > GetScrollBar(0).GetOuterRect().left &&
				cliparea.right <= GetScrollBar(0).GetOuterRect().right)
				cliparea.right = GetScrollBar(0).GetOuterRect().left;

			if (cliparea.left >= GetScrollBar(0).GetOuterRect().left &&
				cliparea.left < GetScrollBar(0).GetOuterRect().right)
				cliparea.left = GetScrollBar(0).GetOuterRect().right;
		}

		// Draw all items for that column
		xpos = 0;
		for (size_t col = 0; col < objectsCount; ++col)
		{
			if (GUI<bool>::GetSetting(this, "hidden_" + m_Columns[col].m_Id))
				continue;

			// Determine text position and width
			const CPos textPos = rect.TopLeft() + CPos(xpos, -scroll + m_ItemsYPositions[i]);

			float width = m_Columns[col].m_Width;
			// Check if it's a decimal value, and if so, assume relative positioning.
			if (m_Columns[col].m_Width < 1 && m_Columns[col].m_Width > 0)
				width *= m_TotalAvailableColumnWidth;

			// Clip text to the column (to prevent drawing text into the neighboring column)
			CRect cliparea2 = cliparea;
			cliparea2.right = std::min(cliparea2.right, textPos.x + width);
			cliparea2.bottom = std::min(cliparea2.bottom, textPos.y + rowHeight);

			// Draw list item
			DrawText(objectsCount * (i +/*Heading*/1) + col, m_Columns[col].m_TextColor, textPos, bz + 0.1f, cliparea2);
			xpos += width;
		}
	}
}
