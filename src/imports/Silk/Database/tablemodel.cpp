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

#include "tablemodel.h"
#include "database.h"

#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QMetaObject>
#include <QtCore/QMetaProperty>
#include <QtCore/QStringList>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlDriver>
#include <QtSql/QSqlError>
#include <QtSql/QSqlRecord>
#include <QtSql/QSqlQuery>

class TableModel::Private : public QObject
{
    Q_OBJECT
public:
    Private(TableModel *parent);
    ~Private();
    void init();

    QString selectSql() const;
    QString toSql(const QVariant &value);

private slots:
    void databaseChanged(Database *database);
    void openChanged(bool open);
    void create();
    void select();

private:
    TableModel *q;
    QStringList initialProperties;
    QMap<QString, QString> ifNotExistsMap;
    QMap<QString, QString> autoIncrementMap;
    QMap<QString, QString> primaryKeyMap;
    QStringList fieldNames;

public:
    Database *database;
    QString name;
    QString primaryKey;
    QList<QVariantList> data;
    QHash<int, QByteArray> roleNames;
};

TableModel::Private::Private(TableModel *parent)
    : QObject(parent)
    , q(parent)
    , database(0)
{
    ifNotExistsMap.insert("QSQLITE", " IF NOT EXISTS");
    ifNotExistsMap.insert("QMYSQL", " IF NOT EXISTS");
    ifNotExistsMap.insert("QPSQL", " IF NOT EXISTS");
    autoIncrementMap.insert("QSQLITE", " AUTOINCREMENT");
    autoIncrementMap.insert("QMYSQL", " AUTO_INCREMENT");
    autoIncrementMap.insert("QPSQL", "");
    primaryKeyMap.insert("QSQLITE", " PRIMARY KEY");
    primaryKeyMap.insert("QMYSQL", " PRIMARY KEY");
    primaryKeyMap.insert("QPSQL", " PRIMARY KEY");

    connect(q, SIGNAL(databaseChanged(Database*)), this, SLOT(databaseChanged(Database*)));
    connect(q, SIGNAL(selectChanged(bool)), this, SLOT(select()));
    const QMetaObject *mo = q->metaObject();
    for (int i = 0; i < mo->propertyCount(); i++) {
        QMetaProperty property = mo->property(i);
//        qDebug() << Q_FUNC_INFO << __LINE__ << property.name() << property.typeName();
        initialProperties.append(property.name());
    }
}

TableModel::Private::~Private()
{
}

void TableModel::Private::init()
{
    if(fieldNames.isEmpty()) {
        const QMetaObject *mo = q->metaObject();
        int j = 0;
        for (int i = initialProperties.count(); i < mo->propertyCount(); i++) {
            QMetaProperty property = mo->property(i);
            roleNames.insert(Qt::UserRole + j, QByteArray(property.name()));
            fieldNames.append(property.name());
            j++;
        }
    }

    if (!database) {
        q->setDatabase(qobject_cast<Database *>(q->QObject::parent()));
    }
}

void TableModel::Private::databaseChanged(Database *database)
{
    disconnect(this, SLOT(openChanged(bool)));
    if (database) {
        connect(database, SIGNAL(openChanged(bool)), this, SLOT(openChanged(bool)));
        openChanged(database->open());
    }
}

void TableModel::Private::openChanged(bool open)
{
    if (open) {
        if (q->name().isEmpty()) {
            qWarning() << "table name is empty.";
            return;
        }
        create();
        select();
    }
}

void TableModel::Private::create()
{
    if (fieldNames.isEmpty()) return;
    QSqlDatabase db = QSqlDatabase::database(database->connectionName());
    if (db.tables().contains(q->name().toLower())) return;

    QString type = db.driverName();

    QString sql = QString("CREATE TABLE%2 %1 (").arg(q->name()).arg(ifNotExistsMap.value(type));

    const QMetaObject *mo = q->metaObject();
    int start = initialProperties.count();
    for (int i = start; i < mo->propertyCount(); i++) {
        QMetaProperty property = mo->property(i);
        if (i > start)
            sql.append(QLatin1String(", "));
        sql.append(property.name());

        bool isPrimaryKey = (primaryKey == QString(property.name()));

        switch (property.type()) {
        case QVariant::Int:
            if (type == QLatin1String("QPSQL") && isPrimaryKey)
                sql += QString(" SERIAL");
            else
                sql += QString(" INTEGER");
            break;
        case QVariant::String:
            sql += QString(" TEXT");
            break;
        case QVariant::Bool:
            sql += QString(" bool");
            break;
        case QVariant::Double:
            sql += QString(" DOUBLE");
            break;
        case QVariant::DateTime:
            sql += QString(" TIMESTAMP");
            break;
        default:
            qWarning() << property.typeName() << "is not supported.";
            break;
        }

        if (isPrimaryKey) {
            sql += primaryKeyMap.value(type);
            if (property.type() == QVariant::Int) {
                sql += autoIncrementMap.value(type);
            }
        } else {
            QVariant value = property.read(q);
            if (!value.isNull()) {
                if (property.type() == QVariant::String || property.type() == QVariant::DateTime) {
                    sql += QString(" DEFAULT '%1'").arg(value.toString());
                } else {
                    sql += QString(" DEFAULT %1").arg(value.toString());
                }
            }
        }
//        qDebug() << Q_FUNC_INFO << __LINE__ << property.name() << property.typeName() << property.read(q) << property.read(q).isNull();
    }

    sql.append(")");
    if (type == QLatin1String("QPSQL"))
        sql.append(" WITH oids");
    QSqlQuery query(sql, db);
    if (!query.exec()) {
        qWarning() << Q_FUNC_INFO << __LINE__ << sql << query.lastError().text();
    }
//    qDebug() << Q_FUNC_INFO << __LINE__;
}

QString TableModel::Private::selectSql() const
{
    QString ret = QString("SELECT %2 FROM %1").arg(q->name()).arg(fieldNames.isEmpty() ? "*" : fieldNames.join(", "));
    if (!q->m_condition.isEmpty())
        ret += QString(" WHERE %1").arg(q->m_condition);
    if (!q->m_order.isEmpty())
        ret += QString(" ORDER BY %1").arg(q->m_order);
    if (q->m_limit > 0) {
        ret += QString(" LIMIT %1").arg(q->m_limit);
        if (q->m_offset > 0) {
            ret += QString(" OFFSET %1").arg(q->m_offset);
        }
    }
    return ret;
}

void TableModel::Private::select()
{
    if (!q->m_select) return;

    QSqlDatabase db = QSqlDatabase::database(database->connectionName());
    q->beginRemoveRows(QModelIndex(), 0, data.count() - 1);
    data.clear();
    q->endRemoveRows();
    QSqlQuery query(selectSql(), db);
    if (roleNames.isEmpty()) {
        QSqlRecord record = query.record();
        for (int i = 0; i < record.count(); i++) {
            roleNames.insert(i + Qt::UserRole, record.fieldName(i).toUtf8());
        }
    }
    while (query.next()) {
        QVariantList d;
        for (int i = 0; i < roleNames.keys().count(); i++) {
            d.append(query.value(i));
        }
        data.append(d);
    }
    q->beginInsertRows(QModelIndex(), 0, data.count() - 1);
    q->endInsertRows();
    emit q->countChanged(data.count());
}

QString TableModel::Private::toSql(const QVariant &value)
{
    switch (value.type()) {
    case QVariant::Int:
        return QString::number(value.toInt());
        break;
    case QVariant::LongLong:
        return QString::number(value.toLongLong());
        break;
    case QVariant::Double:
        return QString::number(value.toDouble());
        break;
    case QVariant::Bool:
        return value.toBool() ? "True" : "False";
        break;
    case QVariant::String:
        return QString("'%1'").arg(value.toString());
        break;
    case QVariant::DateTime:
        // TODO: format
        return value.toDateTime().toString("'yyyy-MM-dd hh:mm:ss'");
        break;
    default:
        qDebug() << Q_FUNC_INFO << __LINE__ << value.type() << "is not handled here.";
        break;
    }
    return QString();
}

TableModel::TableModel(QObject *parent)
    : QAbstractListModel(parent)
    , d(new Private(this))
    , m_select(true)
    , m_limit(0)
    , m_offset(0)
{
}

void TableModel::classBegin()
{

}

void TableModel::componentComplete()
{
    d->init();
}

Database *TableModel::database() const
{
    return d->database;
}

void TableModel::setDatabase(Database *database)
{
    if (d->database == database) return;
    d->database = database;
    emit databaseChanged(database);
}

const QString &TableModel::name() const
{
    return d->name;
}

void TableModel::setName(const QString &name)
{
    if (d->name == name) return;
    d->name = name;
    emit nameChanged(name);
}

const QString &TableModel::primaryKey() const
{
    return d->primaryKey;
}

void TableModel::setPrimaryKey(const QString &primaryKey)
{
    if (d->primaryKey == primaryKey) return;
    d->primaryKey = primaryKey;
    emit primaryKeyChanged(primaryKey);
}

QHash<int, QByteArray> TableModel::roleNames() const
{
    return d->roleNames;
}

int TableModel::rowCount(const QModelIndex &parent) const
{
    return d->data.count();
}

QVariant TableModel::data(const QModelIndex &index, int role) const
{
    if (role >= Qt::UserRole) {
        return d->data.at(index.row()).at(role - Qt::UserRole);
    }
    return QVariant();
}

int TableModel::count() const
{
    return rowCount();
}

QVariantMap TableModel::get(int index) const
{
    QVariantMap ret;
    QVariantList list = d->data.at(index);
    for (int i = 0; i < list.length(); i++) {
//        qDebug() << Q_FUNC_INFO << __LINE__ << i << QString::fromUtf8(d->roleNames.value(Qt::UserRole + i)) << list.at(i);
        ret.insert(QString::fromUtf8(d->roleNames.value(Qt::UserRole + i)), list.at(i));
    }
    return ret;
}

QVariant TableModel::insert(const QVariantMap &data)
{
    QVariant ret;
    QStringList keys;
    QStringList placeHolders;
    QVariantMap values;
    foreach (const QByteArray &r, d->roleNames.values()) {
        QString field = QString::fromUtf8(r);
        if (data.contains(field)) {
            keys.append(field);
            placeHolders.append(QString(":%1").arg(field));
            values.insert(QString(":%1").arg(field), data.value(field));
        }
    }

    QSqlDatabase db = QSqlDatabase::database(d->database->connectionName());
    QSqlQuery query(db);
    if (!query.prepare(QString("INSERT INTO %1(%2) VALUES(%3)").arg(name()).arg(keys.join(", ")).arg(placeHolders.join(", ")))) {
        qWarning() << Q_FUNC_INFO << __LINE__ << query.lastQuery() << query.lastError().text();
        return false;
    }
    foreach (const QString &key, values.keys()) {
        query.bindValue(key, values.value(key));
    }

    if (query.exec()) {
        QString condition = m_condition;
        if (db.driverName() == QLatin1String("QPSQL")) {
            m_condition = QString("%1=%2").arg("oid").arg(query.lastInsertId().toInt());
        } else {
            m_condition = QString("%1=%2").arg(d->primaryKey).arg(d->toSql(query.lastInsertId()));
        }
        QString sql = d->selectSql();
        m_condition = condition;

//        qDebug() << Q_FUNC_INFO << __LINE__ << sql << query.lastInsertId() << db.driver()->hasFeature(QSqlDriver::LastInsertId);
        QSqlQuery query2(sql, db);
        if (query2.first()) {
            int row = rowCount();
            beginInsertRows(QModelIndex(), row, row);
            QVariantList v;
            for (int i = 0; i < d->roleNames.keys().count(); i++) {
//                qDebug() << Q_FUNC_INFO << __LINE__ << i << d->roleNames.value(Qt::UserRole + i) << d->primaryKey;
                if (d->roleNames.value(Qt::UserRole + i) == d->primaryKey) {
                    ret = query2.value(i);
                }
                v.append(query2.value(i));
            }
            d->data.append(v);
            endInsertRows();
            emit countChanged(d->data.count());
        } else {
            qDebug() << Q_FUNC_INFO << __LINE__ << query2.lastError().text() << query2.lastQuery() << query2.boundValues();
        }
    } else {
        qWarning() << Q_FUNC_INFO << __LINE__ << query.lastQuery() << query.boundValues() << query.lastError().text();
    }
    return ret;
}

void TableModel::update(const QVariantMap &data)
{
    QStringList sets;
    QVariantMap values;
    QString where;

    int keyRole = -1;
    QVariant key;

    foreach (int i, d->roleNames.keys()) {
        QString field = QString::fromUtf8(d->roleNames.value(i));
        if (data.contains(field)) {
            QVariant value = data.value(field);
            if (field == d->primaryKey) {
                keyRole = i;
                key = value;
                where = QString(" WHERE %1=%2").arg(field).arg(d->toSql(value));
            } else {
                sets.append(QString("%1=:%1").arg(field));
                values.insert(QString(":%1").arg(field), value);
            }
        }
    }

    QSqlDatabase db = QSqlDatabase::database(d->database->connectionName());
    QSqlQuery query(db);
    if (!query.prepare(QString("UPDATE %1 SET %2%3").arg(name()).arg(sets.join(", ")).arg(where))) {
        qWarning() << Q_FUNC_INFO << __LINE__ << query.lastQuery() << query.lastError().text();
        return;
    }
    foreach (const QString &key, values.keys()) {
        qDebug() << key << values.value(key);
        query.bindValue(key, values.value(key));
    }
    qDebug() << query.boundValues();

    if (query.exec()) {
        for (int i = 0; i < d->data.count(); i++) {
            if (d->data.at(i).at(keyRole - Qt::UserRole) == key) {
                QVariantList newData = d->data.at(i);
                QVector<int> roles;
                foreach (int j, d->roleNames.keys()) {
                    QString field = QString::fromUtf8(d->roleNames.value(j));
                    if (data.contains(field)) {
                        QVariant value = data.value(field);
                        if (field == d->primaryKey) {
                        } else {
                            newData[j - Qt::UserRole] = value;
                            roles.append(j);
                        }
                    }
                }
                d->data[i] = newData;
                dataChanged(index(i), index(i), roles);
                break;
            }
        }
    }
}

bool TableModel::remove(const QVariantMap &data)
{
    QSqlDatabase db = QSqlDatabase::database(d->database->connectionName());
    QString sql = QString("DELETE FROM %1 WHERE %2=%3;").arg(name()).arg(primaryKey()).arg(d->toSql(data.value(primaryKey())));
    QSqlQuery query(sql, db);
    bool ret = query.exec();
    if (ret) {
        int count = rowCount();
        int primaryKeyIndex = -1;
        foreach (int role, d->roleNames.keys()) {
            if (d->roleNames.value(role) == d->primaryKey.toUtf8()) {
                primaryKeyIndex = role;
                break;
            }
        }
        if (primaryKeyIndex > -1) {
            for (int i = 0; i < count; i++) {
                if (d->data.at(i).at(primaryKeyIndex - Qt::UserRole) == data.value(primaryKey())) {
                    beginRemoveRows(QModelIndex(), i, i);
                    d->data.removeAt(i);
                    endRemoveRows();
                    emit countChanged(d->data.count());
                    break;
                }
            }
        }
    } else {
        qWarning() << Q_FUNC_INFO << __LINE__ << sql << query.lastError().text();
    }
    return ret;
}

#include "tablemodel.moc"
