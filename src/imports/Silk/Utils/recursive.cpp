/* Copyright (c) 2012 Silk Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Silk nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL SILK BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "recursive.h"

#include <QtCore/QDebug>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQml/QJSValue>

Recursive::Recursive(QObject *parent)
    : SilkAbstractHttpObject(parent)
    , m_target(nullptr)
{
}

QString Recursive::out()
{
    QString ret;

//    qDebug() << Q_FUNC_INFO << __LINE__ << m_target << m_child;
    QVariant child = m_child;
    static int qjsType = qRegisterMetaType<QJSValue>();
    if (child.type() == qjsType) {
        child = child.value<QJSValue>().toVariant();
    }


    if (m_target && child.type() == QVariant::List) {
        QObject *object = m_target->create(m_target->creationContext());
        SilkAbstractHttpObject *http = qobject_cast<SilkAbstractHttpObject *>(object);
        if (http) {
            http->setProperty("model", child);
            ret.append(http->out());
        }
        object->deleteLater();
    } else {
        qDebug() << Q_FUNC_INFO << __LINE__ << child.type() << (int)child.type() << child;
    }

//    qDebug() << Q_FUNC_INFO << __LINE__ << ret;
    return ret;
}
