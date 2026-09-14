#pragma once

#include <QtQuick3D/QQuick3DGeometry>

class SkinCuboidGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    Q_PROPERTY(Part part READ part WRITE setPart NOTIFY partChanged)
    Q_PROPERTY(bool slim READ slim WRITE setSlim NOTIFY shapeChanged)
    Q_PROPERTY(bool outerLayer READ outerLayer WRITE setOuterLayer NOTIFY shapeChanged)

public:
    enum class Part { Head, Body, RightArm, LeftArm, RightLeg, LeftLeg };
    Q_ENUM(Part)

    explicit SkinCuboidGeometry(QQuick3DObject *parent = nullptr);
    [[nodiscard]] Part part() const noexcept { return m_part; }
    void setPart(Part part);
    bool slim() const noexcept { return m_slim; }
    bool outerLayer() const noexcept { return m_outerLayer; }
    void setSlim(bool value);
    void setOuterLayer(bool value);

signals:
    void partChanged();
    void shapeChanged();

private:
    void rebuild();
    Part m_part = Part::Head;
    bool m_slim = false;
    bool m_outerLayer = false;
};
