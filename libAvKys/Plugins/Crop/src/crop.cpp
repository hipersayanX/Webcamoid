/* Webcamoid, webcam capture application. Crop Plug-in.
 * Copyright (C) 2022  Tj <hacker@iam.tj>
 *
 * Webcamoid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Webcamoid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Webcamoid. If not, see <http://www.gnu.org/licenses/>.
 *
 * Web-Site: http://webcamoid.github.io/
 */

#include <QQmlEngine>

#include "crop.h"
#include "cropelement.h"

QObject *Crop::create(const QString &key, const QString &specification)
{
    Q_UNUSED(key)
    Q_UNUSED(specification)
    qmlRegisterType<CropElement>("CropElement", 1, 0, "CropElement");

    return new CropElement();
}

QStringList Crop::keys() const
{
    return {};
}

#include "moc_crop.cpp"
