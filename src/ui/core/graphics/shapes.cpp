#include <ui/ui.h>

namespace ui
{

void shape(Context& context, Rect bounds, const ShapeData& data, Color fill, Color border, float borderWidth, float radius)
{
    if (bounds.w <= 0 || bounds.h <= 0) return;
    if (data.kind == ShapeKind::Rect) {
        context.rect(bounds, fill, radius, border, borderWidth);
        return;
    }
    auto key = paintKey('S', data.kind, bounds, data.viewWidth, data.viewHeight, fill, border, borderWidth);
    context.paint(paintHash({reinterpret_cast<const char*>(data.path.data()), data.path.size() * sizeof(PathSegment)}, key), [&] {
        auto paint = tvg::Shape::gen();
        if (data.kind == ShapeKind::Ellipse) {
            paint->appendCircle(bounds.x + bounds.w / 2, bounds.y + bounds.h / 2, bounds.w / 2, bounds.h / 2);
        } else {
            auto x = [&](float v) {
                return bounds.x + v * bounds.w / data.viewWidth;
            };
            auto y = [&](float v) {
                return bounds.y + v * bounds.h / data.viewHeight;
            };
            for (const auto& segment : data.path) {
                const auto& p = segment.points;
                switch (segment.command) {
                    case PathCommand::Move: paint->moveTo(x(p[0]), y(p[1])); break;
                    case PathCommand::Line: paint->lineTo(x(p[0]), y(p[1])); break;
                    case PathCommand::Cubic: paint->cubicTo(x(p[0]), y(p[1]), x(p[2]), y(p[3]), x(p[4]), y(p[5])); break;
                    case PathCommand::Close: paint->close(); break;
                }
            }
        }
        paint->fill(fill.r, fill.g, fill.b, fill.a);
        if (border.a && borderWidth > 0) {
            paint->strokeWidth(borderWidth);
            paint->strokeFill(border.r, border.g, border.b, border.a);
        }
        return paint;
    });
}

}  // namespace ui
