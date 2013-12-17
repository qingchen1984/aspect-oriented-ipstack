/*
 * Copyright 2013 David Graeff
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE REGENTS AND CONTRIBUTORS, INC. OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "kconfigModel.h"
#include <QFile>
#include <QDebug>
#include <QColor>

DependencyModel::DependencyModel(const QString& kconfig_input_filename, QObject *parent)
    : QAbstractItemModel(parent), rootItem(0), kconfig_input_filename(kconfig_input_filename), usedEntries(0)
{
    createModel();
}

DependencyModel::~DependencyModel()
{
    delete rootItem;
}

void DependencyModel::createModel()
{
    beginResetModel();
    delete rootItem;
    usedEntries = 0;
    rootItem = new DependencyModelItem();

    QFile file(kconfig_input_filename);
    if (!file.open(QFile::ReadOnly)) {
        qWarning() << "Failed to open kconfig input file" << kconfig_input_filename;
        endResetModel();
        return;
    }

    QList<DependencyModelItem*> items;

    DependencyModelItem* currentItem = 0;
    bool inhelp = false;
    bool inChoice = false;
    QString choiceDepends;

    while(!file.atEnd()) {
        QString line = file.readLine().trimmed();
        if (line.startsWith("menuconfig ")) {
            currentItem = new DependencyModelItem;
            currentItem->model = this;
            currentItem->name = line.remove(0, 11).trimmed();
            continue;
        }
        if (line.startsWith("config ")) {
            currentItem = new DependencyModelItem;
            currentItem->model = this;
            currentItem->name = line.remove(0, 7).trimmed();
            continue;
        }
        if (line.compare("choice")==0) {
            inChoice = true;
            continue;
        }
        if (line.compare("endchoice")==0) {
            inChoice = false;
            choiceDepends.clear();
            continue;
        }

        if (inChoice && line.startsWith("depends on ")) {
            choiceDepends = line.remove(0, 11).trimmed();
        }

        if (currentItem) {
            if (line.isEmpty()) {
                if (inhelp) {
                    // Break long help line into multiple lines
                    currentItem->description = "<p>" + currentItem->description + "</p>";
                }
                inhelp = false;
                if (currentItem->text.size()) {
                    items.append(currentItem);
                    itemList.insert(currentItem->name, currentItem);
                } else
                    delete currentItem;
                currentItem = 0;
                continue;
            }
            if (inhelp) {
                currentItem->description += line + " ";
            } else if (line.startsWith("bool ")) {
                currentItem->text = line.remove(0, 5).replace('"',"");
                if (choiceDepends.size()) {
                    currentItem->depends = choiceDepends;
                    int i = currentItem->depends.indexOf(' ');
                    if (i!=-1)
                        currentItem->depends = currentItem->depends.left(i);
                }
            } else if (line.startsWith("depends on ")) {
                currentItem->depends = line.remove(0, 11).trimmed();
                int i = currentItem->depends.indexOf(' ');
                if (i!=-1)
                    currentItem->depends = currentItem->depends.left(i);
            } else if (line.startsWith("help")) {
                inhelp = true;
            }
        }
    }

    int before, after;
    do {
        before = items.size();

        for(int i=items.size()-1;i>=0;--i) {
            DependencyModelItem* foundParent = itemList.value(items[i]->depends);
            if (foundParent) {
                items[i]->parent = foundParent;
                items[i]->row = foundParent->childs.size();
                foundParent->insertSorted(items[i]);
                items.removeAt(i);
            }
        }

        after = items.size();
    } while (before != after && after>0);

    // add rest of items
    for(int i=items.size()-1;i>=0;--i) {
        items[i]->parent = rootItem;
        items[i]->row = rootItem->childs.size();
        rootItem->insertSorted(items[i]);
    }

    endResetModel();
}

void DependencyModel::resetAllUsed()
{
    beginResetModel();
    usedEntries = 0;
    auto it = itemList.begin();
    while(it!=itemList.end()) {
        (*it)->used = false;
        ++it;
    }
    endResetModel();
}

void DependencyModel::markUsed(const QString &dependencyName, bool used)
{
    DependencyModelItem* item = getItemByName(dependencyName);
    if (!item || item->used == used)
        return;

    // update used counter
//    qDebug() << "markUsed" << dependencyName << used << item;
    usedEntries = usedEntries + (used?1:-1);
    item->used = used;
    QModelIndex index = createIndex(item->row, 0, item);
    emit dataChanged(index, index);
}

void DependencyModel::updateDependency(const QString &oldDepString, const QString &newDepString)
{
    QStringList usedlist = oldDepString.split(' ');
    foreach(const QString& used, usedlist) {
        if (used.startsWith("&"))
            markUsed(used.mid(1), false);
    }
    usedlist = newDepString.split(' ');
    foreach(const QString& used, usedlist) {
        if (used.startsWith("&"))
            markUsed(used.mid(1), true);
    }
}

int DependencyModel::columnCount(const QModelIndex &) const
{
    return 2;
}

QVariant DependencyModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid())
		return QVariant();

    DependencyModelItem *item = static_cast<DependencyModelItem*>(index.internalPointer());

    if (role==Qt::DisplayRole) {
        switch (index.column()) {
            case 0: return item->text;
            case 1: return item->name;
        }
    } else if (role==Qt::ForegroundRole) {
        return item->used ? QColor(Qt::green) : QColor(Qt::TextColorRole);
    } else
    if (role==Qt::ToolTipRole) {
        return item->description;
    }
    return QVariant();
}

QString DependencyModel::feature_name(const QModelIndex &current) {
    DependencyModelItem *item = static_cast<DependencyModelItem*>(current.internalPointer());
    return item->name;
}

QModelIndex DependencyModel::indexOf(const QString &name)
{
    DependencyModelItem* d = itemList.value(name);
    if (d) {
        return createIndex(d->row, 0, d);
    } else
        return QModelIndex();
}

int DependencyModel::count()
{
    return itemList.size();
}

DependencyModelItem *DependencyModel::getRootItem()
{
    return rootItem;
}

DependencyModelItem* DependencyModel::getItemByName(const QString& name)
{
    return itemList.value(name);
}

QVariant DependencyModel::headerData(int section, Qt::Orientation orientation,
							int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case 0: return tr("Option");
        case 1: return tr("Name");
        }
    }
    else if (orientation == Qt::Horizontal && role == Qt::ToolTipRole) {
        switch (section) {
        case 0: return tr("Option");
        case 1: return tr("Internal Name");
        }
    }
	return QVariant();
}

QModelIndex DependencyModel::index(int row, int column, const QModelIndex &parent)
			const
{
	if (!hasIndex(row, column, parent))
		return QModelIndex();

    DependencyModelItem *parentItem;

	if (!parent.isValid())
		parentItem = rootItem;
	else
        parentItem = static_cast<DependencyModelItem*>(parent.internalPointer());

    DependencyModelItem *childItem = parentItem->childs.at(row);
	if (childItem)
		return createIndex(row, column, childItem);
	else
		return QModelIndex();
}

QModelIndex DependencyModel::parent(const QModelIndex &index) const
{
	if (!index.isValid())
		return QModelIndex();

    DependencyModelItem *childItem = static_cast<DependencyModelItem*>(index.internalPointer());
    DependencyModelItem *parentItem = childItem->parent;

	if (parentItem == rootItem)
		return QModelIndex();

    return createIndex(parentItem->row, 0, parentItem);
}

int DependencyModel::rowCount(const QModelIndex &parent) const
{
    DependencyModelItem *parentItem;
	if (parent.column() > 0)
		return 0;

	if (!parent.isValid())
		parentItem = rootItem;
	else
        parentItem = static_cast<DependencyModelItem*>(parent.internalPointer());

    return parentItem->childs.size();
}


int DependencyModelItem::binary_search_text_lower_bound(const QString &text)
{
    int half=0, begin=0, middle=0, n = childs.size();
    while (n>0) {
        half = n >> 1;
        middle = begin + half;
        if (childs[middle]->text < text) {
            begin = middle + 1;
            n -= half + 1;
        } else {
            n = half;
        }
    }
    return begin;
}
