/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "stem.h"

#include <cmath>

#include "io/xml.h"
#include "draw/brush.h"

#include "staff.h"
#include "chord.h"
#include "score.h"
#include "stafftype.h"
#include "hook.h"
#include "tremolo.h"
#include "note.h"

// Part of the dot-drawing hack in Stem::draw()
#include "symid.h"
//--

using namespace mu;
using namespace mu::draw;
using namespace mu::engraving;
using namespace Ms;

static const ElementStyle stemStyle {
    { Sid::stemWidth, Pid::LINE_WIDTH }
};

Stem::Stem(Chord* parent)
    : EngravingItem(ElementType::STEM, parent)
{
    initElementStyle(&stemStyle);
    resetProperty(Pid::USER_LEN);
}

EngravingItem* Stem::elementBase() const
{
    return parentItem();
}

int Stem::vStaffIdx() const
{
    return staffIdx() + chord()->staffMove();
}

bool Stem::up() const
{
    return chord() ? chord()->up() : true;
}

//! For beamed notes this is called twice. The final stem length
//! can only be calculated after stretching of the measure. We
//! need a guessed stem shape to calculate the minimal distance
//! between segments. The guessed stem must have at least the
//! right direction.
void Stem::layout()
{
    const bool up = this->up();
    const double _up = up ? -1.0 : 1.0;

    double y1 = 0.0; // vertical displacement to match note attach point
    double y2 = _up * (m_baseLength + m_userLength);

    bool isTabStaff = false;
    if (chord()) {
        setMag(chord()->mag());

        const Staff* staff = this->staff();
        const StaffType* staffType = staff ? staff->staffTypeForElement(chord()) : nullptr;
        isTabStaff = staffType && staffType->isTabStaff();

        if (isTabStaff) {
            if (staffType->stemThrough()) {
                // if stems through staves, gets Y pos. of stem-side note relative to chord other side
                const double staffLinesDistance = staffType->lineDistance().val() * spatium();
                y1 = (chord()->downString() - chord()->upString()) * _up * staffLinesDistance;

                // if fret marks above lines, raise stem beginning by 1/2 line distance
                if (!staffType->onLines()) {
                    y1 -= staffLinesDistance * 0.5;
                }

                // shorten stem by 1/2 lineDist to clear the note and a little more to keep 'air' between stem and note
                y1 += _up * staffLinesDistance * 0.7;
            }
            // in other TAB types, no correction
        } else { // non-TAB
            // move stem start to note attach point
            Note* note = up ? chord()->downNote() : chord()->upNote();
            if ((up && !note->mirror()) || (!up && note->mirror())) {
                y1 = note->stemUpSE().y();
            } else {
                y1 = note->stemDownNW().y();
            }

            rypos() = note->rypos();
        }

        if (chord()->hook() && !chord()->beam()) {
            y2 += chord()->hook()->smuflAnchor().y();
        }
    }

    double lineWidthCorrection = lineWidthMag() * 0.5;
    double lineX = isTabStaff ? 0.0 : _up * lineWidthCorrection;
    m_line.setLine(lineX, y1, lineX, y2);

    // compute line and bounding rectangle
    RectF rect(m_line.p1(), m_line.p2());
    setbbox(rect.normalized().adjusted(-lineWidthCorrection, 0, lineWidthCorrection, 0));
}

void Stem::setBaseLength(double baseLength)
{
    m_baseLength = std::abs(baseLength);
    layout();
}

void Stem::spatiumChanged(double oldValue, double newValue)
{
    m_userLength = (m_userLength / oldValue) * newValue;
    layout();
}

//! In chord coordinates
PointF Stem::flagPosition() const
{
    return pos() + PointF(_bbox.left(), up() ? -length() : length());
}

void Stem::draw(mu::draw::Painter* painter) const
{
    TRACE_OBJ_DRAW;
    if (!chord()) { // may be need assert?
        return;
    }

    // hide if second chord of a cross-measure pair
    if (chord()->crossMeasure() == CrossMeasure::SECOND) {
        return;
    }

    const Staff* staff = this->staff();
    const StaffType* staffType = staff ? staff->staffTypeForElement(chord()) : nullptr;
    const bool isTablature = staffType && staffType->isTabStaff();

    painter->setPen(Pen(curColor(), lineWidthMag(), PenStyle::SolidLine, PenCapStyle::FlatCap));
    painter->drawLine(m_line);

    if (!isTablature) {
        return;
    }

    // TODO: adjust bounding rectangle in layout() for dots and for slash
    qreal sp = spatium();
    bool isUp = up();

    // slashed half note stem
    if (chord()->durationType().type() == TDuration::DurationType::V_HALF
        && staffType->minimStyle() == TablatureMinimStyle::SLASHED) {
        // position slashes onto stem
        qreal y = isUp ? -length() + STAFFTYPE_TAB_SLASH_2STARTY_UP * sp
                  : length() - STAFFTYPE_TAB_SLASH_2STARTY_DN * sp;
        // if stems through, try to align slashes within or across lines
        if (staffType->stemThrough()) {
            qreal halfLineDist = staffType->lineDistance().val() * sp * 0.5;
            qreal halfSlashHgt = STAFFTYPE_TAB_SLASH_2TOTHEIGHT * sp * 0.5;
            y = lrint((y + halfSlashHgt) / halfLineDist) * halfLineDist - halfSlashHgt;
        }
        // draw slashes
        qreal hlfWdt= sp * STAFFTYPE_TAB_SLASH_WIDTH * 0.5;
        qreal sln   = sp * STAFFTYPE_TAB_SLASH_SLANTY;
        qreal thk   = sp * STAFFTYPE_TAB_SLASH_THICK;
        qreal displ = sp * STAFFTYPE_TAB_SLASH_DISPL;
        PainterPath path;
        for (int i = 0; i < 2; ++i) {
            path.moveTo(hlfWdt, y);                   // top-right corner
            path.lineTo(hlfWdt, y + thk);             // bottom-right corner
            path.lineTo(-hlfWdt, y + thk + sln);      // bottom-left corner
            path.lineTo(-hlfWdt, y + sln);            // top-left corner
            path.closeSubpath();
            y += displ;
        }
        painter->setBrush(Brush(curColor()));
        painter->setNoPen();
        painter->drawPath(path);
    }

    // dots
    // NOT THE BEST PLACE FOR THIS?
    // with tablatures and stems beside staves, dots are not drawn near 'notes', but near stems
    int nDots = chord()->dots();
    if (nDots > 0 && !staffType->stemThrough()) {
        qreal x     = chord()->dotPosX();
        qreal y     = ((STAFFTYPE_TAB_DEFAULTSTEMLEN_DN * 0.2) * sp) * (isUp ? -1.0 : 1.0);
        qreal step  = score()->styleS(Sid::dotDotDistance).val() * sp;
        for (int dot = 0; dot < nDots; dot++, x += step) {
            drawSymbol(SymId::augmentationDot, painter, PointF(x, y));
        }
    }
}

void Stem::write(XmlWriter& xml) const
{
    xml.startObject(this);
    EngravingItem::writeProperties(xml);
    writeProperty(xml, Pid::USER_LEN);
    writeProperty(xml, Pid::LINE_WIDTH);
    xml.endObject();
}

void Stem::read(XmlReader& e)
{
    while (e.readNextStartElement()) {
        if (!readProperties(e)) {
            e.unknown();
        }
    }
}

bool Stem::readProperties(XmlReader& e)
{
    const QStringRef& tag(e.name());

    if (readProperty(tag, e, Pid::USER_LEN)) {
    } else if (readStyledProperty(e, tag)) {
    } else if (EngravingItem::readProperties(e)) {
    } else {
        return false;
    }
    return true;
}

std::vector<mu::PointF> Stem::gripsPositions(const EditData&) const
{
    return { pagePos() + m_line.p2() };
}

void Stem::startEdit(EditData& ed)
{
    EngravingItem::startEdit(ed);
    ElementEditData* eed = ed.getData(this);
    eed->pushProperty(Pid::USER_LEN);
}

void Stem::editDrag(EditData& ed)
{
    double yDelta = ed.delta.y();
    m_userLength += up() ? -yDelta : yDelta;
    layout();
    Chord* c = chord();
    if (c->hook()) {
        c->hook()->move(PointF(0.0, yDelta));
    }
}

void Stem::reset()
{
    undoChangeProperty(Pid::USER_LEN, 0.0);
    EngravingItem::reset();
}

bool Stem::acceptDrop(EditData& data) const
{
    EngravingItem* e = data.dropElement;
    if ((e->type() == ElementType::TREMOLO) && (toTremolo(e)->tremoloType() <= TremoloType::R64)) {
        return true;
    }
    return false;
}

EngravingItem* Stem::drop(EditData& data)
{
    EngravingItem* e = data.dropElement;
    Chord* ch  = chord();

    switch (e->type()) {
    case ElementType::TREMOLO:
        toTremolo(e)->setParent(ch);
        undoAddElement(e);
        return e;
    default:
        delete e;
        break;
    }
    return 0;
}

PropertyValue Stem::getProperty(Pid propertyId) const
{
    switch (propertyId) {
    case Pid::LINE_WIDTH:
        return lineWidth();
    case Pid::USER_LEN:
        return userLength();
    case Pid::STEM_DIRECTION:
        return PropertyValue::fromValue<Direction>(chord()->stemDirection());
    default:
        return EngravingItem::getProperty(propertyId);
    }
}

bool Stem::setProperty(Pid propertyId, const PropertyValue& v)
{
    switch (propertyId) {
    case Pid::LINE_WIDTH:
        setLineWidth(v.toReal());
        break;
    case Pid::USER_LEN:
        setUserLength(v.toDouble());
        break;
    case Pid::STEM_DIRECTION:
        chord()->setStemDirection(v.value<Direction>());
        break;
    default:
        return EngravingItem::setProperty(propertyId, v);
    }
    triggerLayout();
    return true;
}

PropertyValue Stem::propertyDefault(Pid id) const
{
    switch (id) {
    case Pid::USER_LEN:
        return 0.0;
    case Pid::STEM_DIRECTION:
        return PropertyValue::fromValue<Direction>(Direction::AUTO);
    default:
        return EngravingItem::propertyDefault(id);
    }
}
