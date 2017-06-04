#include "qtnode.h"

#include "introspection.h"

#include <QDebug>

#ifdef QT5_SUPPORT
  #include <QtWidgets/QGraphicsScene>
  #include <QtWidgets/QGraphicsObject>
  #include <QtQml/QQmlEngine>
  #include <QtQml/QQmlContext>
  #include <QtQuick/QQuickView>
  #include <QtQuick/QQuickItem>
  #include <QtQuickWidgets/QQuickWidget>
#else
  #include <QGraphicsScene>
  #include <QGraphicsObject>
#endif
#include <QDBusArgument>

#include <QAbstractItemView>
#include <QAbstractProxyModel>
#include <QTableWidget>
#include <QTreeView>
#include <QTreeWidget>
#include <QListView>

const QByteArray AP_ID_NAME("_autopilot_id");

void CollectSpecialChildren(QObject* object, xpathselect::NodeVector& children, DBusNode::Ptr parent);

void GetDataElementChildren(QTableWidget* table, xpathselect::NodeVector& children, DBusNode::Ptr parent);
void GetDataElementChildren(QTreeView* tree_view, xpathselect::NodeVector& children, DBusNode::Ptr parent);
void GetDataElementChildren(QTreeWidget* tree_widget, xpathselect::NodeVector& children, DBusNode::Ptr parent);
void GetDataElementChildren(QListView* list_view, xpathselect::NodeVector& children, DBusNode::Ptr parent);

void CollectAllIndices(QModelIndex index, QAbstractItemModel *model, QModelIndexList &collection);
QVariant SafePackProperty(QVariant const& prop);

bool MatchProperty(QVariantMap const& packed_properties, std::string const& name, QVariant value);

// Produce an id suitable for xpathselects' GetId
int32_t calculate_ap_id(quint64 big_id)
{
    int32_t high = static_cast<int32_t>(big_id >> 32);
    int32_t low = static_cast<int32_t>(big_id);
    return high ^ low;
}

// Marshall the NodeIntrospectionData data into a D-Bus argument
QDBusArgument &operator<<(QDBusArgument &argument, NodeIntrospectionData const& node_data)
{
    argument.beginStructure();
    argument << node_data.object_path << node_data.state;
    argument.endStructure();
    return argument;
}

// Retrieve the NodeIntrospectionData data from the D-Bus argument
const QDBusArgument &operator>>(QDBusArgument const& argument, NodeIntrospectionData& node_data)
{
    argument.beginStructure();
    argument >> node_data.object_path >> node_data.state;
    argument.endStructure();
    return argument;
}

void GetDataElementChildren(QTableWidget *table, xpathselect::NodeVector& children, DBusNode::Ptr parent)
{
    QList<QTableWidgetItem *> tablewidgetitems = table->findItems("*", Qt::MatchWildcard|Qt::MatchRecursive);
    foreach (QTableWidgetItem *item, tablewidgetitems){
        children.push_back(
            std::make_shared<QTableWidgetItemNode>(item, parent)
            );
    }
}

void CollectAllIndices(QModelIndex index, QAbstractItemModel *model, QModelIndexList &collection)
{
    for(int c=0; c < model->columnCount(index); ++c) {
        for(int r=0; r < model->rowCount(index); ++r) {
            QModelIndex new_index = model->index(r, c, index);
            collection.push_back(new_index);
            if(new_index.isValid() && qHash(new_index) != qHash(index)) {
                CollectAllIndices(new_index, model, collection);
            }
        }
    }
}

// Pack property, but return a default blank if the packed property is invalid.
QVariant SafePackProperty(QVariant const& prop)
{
    static QVariant blank_default = PackProperty("");

    QVariant property_attempt = PackProperty(prop);
    if(property_attempt.isValid())
        return property_attempt;
    else
        return blank_default;
}

bool MatchProperty(QVariantMap const& packed_properties, std::string const& name, QVariant value)
{
    QString qname = QString::fromStdString(name);
    if (! packed_properties.contains(qname))
        return false;

    // Because the properties are packed, we need the value, not the type.
    QVariant object_value = qvariant_cast<QVariantList>(packed_properties[qname]).at(1);
    if (value.canConvert(object_value.type()))
    {
        value.convert(object_value.type());
        return value == object_value;
    }
    return false;
}


void GetDataElementChildren(QTreeView* tree_view, xpathselect::NodeVector& children, DBusNode::Ptr parent)
{
    QAbstractItemModel* abstract_model = tree_view->model();
    if(! abstract_model)
    {
        qDebug() << "Unable to get element children from QTreeView "
                 << "with objectName '" << tree_view->objectName() << "'. "
                 << "No model found.";
        return;
    }

    QModelIndexList all_indices;
    for(int c=0; c < abstract_model->columnCount(); ++c) {
        for(int r=0; r < abstract_model->rowCount(); ++r) {
            QModelIndex index = abstract_model->index(r, c);
            all_indices.push_back(index);
            CollectAllIndices(index, abstract_model, all_indices);
        }
    }

    foreach(QModelIndex index, all_indices)
    {
        if(index.isValid())
        {
            children.push_back(
                std::make_shared<QModelIndexNode>(
                    index,
                    tree_view,
                    parent)
                );
        }
    }
}

void GetDataElementChildren(QTreeWidget* tree_widget, xpathselect::NodeVector& children, DBusNode::Ptr parent)
{
    for(int i=0; i < tree_widget->topLevelItemCount(); ++i) {
        children.push_back(
            std::make_shared<QTreeWidgetItemNode>(
                tree_widget->topLevelItem(i),
                parent)
            );
    }
}

void GetDataElementChildren(QListView* list_view, xpathselect::NodeVector& children, DBusNode::Ptr parent)
{
    QAbstractItemModel* abstract_model = list_view->model();

    if(! abstract_model) {
        qDebug() << "Unable to get element children from QListView "
                 << "with objectName '" << list_view->objectName() << "'. "
                 << "No model found.";
        return;
    }

    QModelIndexList all_indices;
    QModelIndex root_index = list_view->rootIndex();
    if(root_index.isValid()) {
        // The root item is the parent item to the view's toplevel items
        CollectAllIndices(root_index, abstract_model, all_indices);
    }
    else {
        for(int c=0; c < abstract_model->columnCount(); ++c) {
            for(int r=0; r < abstract_model->rowCount(); ++r) {
                QModelIndex index = abstract_model->index(r, c);
                all_indices.push_back(index);
                CollectAllIndices(index, abstract_model, all_indices);
            }
        }
    }

    foreach(QModelIndex index, all_indices) {
        if(index.isValid())
        {
            children.push_back(
                std::make_shared<QModelIndexNode>(
                    index,
                    list_view,
                    parent)
                );
        }
    }
}

QObjectNode::QObjectNode(QObject *obj, DBusNode::Ptr parent)
: object_(obj)
, parent_(parent)
{
    std::string parent_path = parent ? parent->GetPath() : "";
    full_path_ = parent_path + "/" + GetName();
}

QObjectNode::QObjectNode(QObject* obj)
: object_(obj)
{
    full_path_ = "/" + GetName();
}

QObject* QObjectNode::getWrappedObject() const
{
    return object_;
}

NodeIntrospectionData QObjectNode::GetIntrospectionData() const
{
    NodeIntrospectionData data;
    data.object_path = QString::fromStdString(GetPath());
    data.state = GetNodeProperties(object_);
    data.state["id"] = PackProperty(GetId());
    return data;
}

std::string QObjectNode::GetName() const
{
    QString name = object_->metaObject()->className();

    // QML type names get mangled by Qt - they get _QML_N or _QMLTYPE_N appended.
    if (name.contains('_'))
        name = name.split('_').front();
    return name.toStdString();
}

std::string QObjectNode::GetPath() const
{
    return full_path_;
}

int32_t QObjectNode::GetId() const
{
    // Note: This method is used to assign ids to both the root node (with a QApplication object) and
    // child nodes. This used to be separate code, but now that we export QApplication properties,
    // we can use this one method everywhere.
    static int32_t next_id=0;

    QList<QByteArray> property_names = object_->dynamicPropertyNames();
    if (!property_names.contains(AP_ID_NAME))
    {
        int32_t new_id = ++next_id;
        object_->setProperty(AP_ID_NAME, QVariant(new_id));
    }
    return qvariant_cast<int32_t>(object_->property(AP_ID_NAME));
}

bool QObjectNode::MatchStringProperty(std::string const& name, std::string const& value) const
{
    return MatchProperty(GetNodeProperties(object_), name, QString::fromStdString(value));
}

bool QObjectNode::MatchIntegerProperty(std::string const& name, int32_t value) const
{
    if (name == "id")
        return value == GetId();

    return MatchProperty(GetNodeProperties(object_), name, value);
}

bool QObjectNode::MatchBooleanProperty(std::string const& name, bool value) const
{
    return MatchProperty(GetNodeProperties(object_), name, value);
}

template <class T>
bool AttemptGetSpecialChildren(QObject* object, xpathselect::NodeVector& children, DBusNode::Ptr parent)
{
    auto className = T::staticMetaObject.className();
    if(object->inherits(className))
    {
        T* table = qobject_cast<T *>(object);
        if(table) {
            GetDataElementChildren(table, children, parent);
        }
        else {
            qDebug() << "Casting object (with objectName: " << object->objectName() << ") "
                     << "to " << className
                     << "failed. Unable to retrieve children.";
            return false;
        }
        return true;
    }
    return false;
}

void CollectSpecialChildren(QObject* object, xpathselect::NodeVector& children, DBusNode::Ptr parent)
{
    // Need to make sure to make these checks in the correct order.
    // i.e. Because QTreeWidget inherits from QTreeView do it first otherwise
    // we would never reach the specific QTreeWidget code.
    AttemptGetSpecialChildren<QTableWidget>(object, children, parent)
        || AttemptGetSpecialChildren<QTreeWidget>(object, children, parent)
        || AttemptGetSpecialChildren<QTreeView>(object, children, parent)
        || AttemptGetSpecialChildren<QListView>(object, children, parent);
}

xpathselect::NodeVector QObjectNode::Children() const
{
    xpathselect::NodeVector children;

    CollectSpecialChildren(object_, children, shared_from_this());

    // Qt5's hierarchy for QML has changed a bit:
    // - On top there's a QQuickView which holds all the QQuick items
    // - QQuickItems don't always follow the QObject type hierarchy (e.g. QQuickListView does not), therefore we use the QQuickItem's childItems()
    // - In case it is not a QQuickItem, fall back to the standard QObject hierarchy

    QQuickView *view = qobject_cast<QQuickView*>(object_);
    if (view && view->rootObject() != 0) {
        children.push_back(std::make_shared<QObjectNode>(view->rootObject(), shared_from_this()));
    }

    QQuickWidget *wview = qobject_cast<QQuickWidget*>(object_);
    if (wview && wview->rootObject() != 0) {
        qDebug() << "Collect QQuickWidget childrens";
        children.push_back(std::make_shared<QObjectNode>(wview->rootObject(), shared_from_this()));
    }

    QQuickWindow *quickWindow = qobject_cast<QQuickWindow*>(object_);
    if (quickWindow) {
        //children.push_back(std::make_shared<QObjectNode>(quickWindow->contentItem(), shared_from_this()));

        // Process data property
        if (quickWindow->property("data").isValid()) {
            QQmlListProperty<QObject> data = qvariant_cast<QQmlListProperty<QObject>>(quickWindow->property("data"));

            for (int index = 0; index < data.count(&data); index++) {
                QObject* item = data.at(&data, index);

                children.push_back(std::make_shared<QObjectNode>(item, shared_from_this()));
            }
        }
    }

    QQuickItem* item = qobject_cast<QQuickItem*>(object_);
    if (item) {
        foreach (QQuickItem *childItem, item->childItems()) {
            if (childItem->parentItem() == item) {
                children.push_back(std::make_shared<QObjectNode>(childItem, shared_from_this()));
            }
        }
    } else {
        foreach (QObject *child, object_->children())
        {
            if (child->parent() == object_)
                children.push_back(std::make_shared<QObjectNode>(child, shared_from_this()));
        }
    }

    return children;
}


xpathselect::Node::Ptr QObjectNode::GetParent() const
{
    return parent_;
}

// QModelIndexNode
QModelIndexNode::QModelIndexNode(QModelIndex index, QAbstractItemView* parent_view, DBusNode::Ptr parent)
    : index_(index)
    , parent_view_(parent_view)
    , parent_(parent)
{
    std::string parent_path = parent ? parent->GetPath() : "";
    full_path_ = parent_path + "/" + GetName();
}

NodeIntrospectionData QModelIndexNode::GetIntrospectionData() const
{
    NodeIntrospectionData data;
    data.object_path = QString::fromStdString(GetPath());
    data.state = GetProperties();
    data.state["id"] = PackProperty(GetId());
    return data;
}

QVariantMap QModelIndexNode::GetProperties() const
{
    QVariantMap properties;
    const QAbstractItemModel* model = index_.model();
    if(model)
    {
        // Make an attempt to store the 'text' of a node to be user friendly-ish.
        properties["text"] = SafePackProperty(model->data(index_));

        // Include any Role data (mung the role name with added "Role")
        const QHash<int, QByteArray> role_names = model->roleNames();
        QMap<int, QVariant> item_data = model->itemData(index_);
        foreach(int name, role_names.keys())
        {
            if(item_data.contains(name)) {
                properties[role_names[name]+"Role"] = SafePackProperty(item_data[name]);
            }
            else {
                properties[role_names[name]+"Role"] = PackProperty("");
            }
        }
    }

    QRect rect = parent_view_->visualRect(index_);
    QRect global_rect(
        parent_view_->viewport()->mapToGlobal(rect.topLeft()),
        rect.size());
    QRect viewport_contents = parent_view_->viewport()->contentsRect();
    properties["onScreen"] = PackProperty(viewport_contents.contains(rect));
    properties["globalRect"] = PackProperty(global_rect);

    return properties;
}

xpathselect::Node::Ptr QModelIndexNode::GetParent() const
{
    return parent_;
}

std::string QModelIndexNode::GetName() const
{
    return "QModelIndex";
}

std::string QModelIndexNode::GetPath() const
{
    return full_path_;
}

int32_t QModelIndexNode::GetId() const
{
    return calculate_ap_id(static_cast<quint64>(qHash(index_)));
}

bool QModelIndexNode::MatchStringProperty(std::string const& name, std::string const& value) const
{
    return MatchProperty(GetProperties(), name, QString::fromStdString(value));
}

bool QModelIndexNode::MatchIntegerProperty(std::string const& name, int32_t value) const
{
    if (name == "id")
        return value == GetId();

    return MatchProperty(GetProperties(), name, value);
}

bool QModelIndexNode::MatchBooleanProperty(std::string const& name, bool value) const
{
    return MatchProperty(GetProperties(), name, value);
}

xpathselect::NodeVector QModelIndexNode::Children() const
{
    // Doesn't have any children.
    xpathselect::NodeVector children;
    return children;
}

// QTableWidgetItemNode
QTableWidgetItemNode::QTableWidgetItemNode(QTableWidgetItem *item, DBusNode::Ptr parent)
    : item_(item)
    , parent_(parent)
{
    std::string parent_path = parent ? parent->GetPath() : "";
    full_path_ = parent_path + "/" + GetName();
}

NodeIntrospectionData QTableWidgetItemNode::GetIntrospectionData() const
{
    NodeIntrospectionData data;
    data.object_path = QString::fromStdString(GetPath());
    data.state = GetProperties();
    data.state["id"] = PackProperty(GetId());
    return data;
}

QVariantMap QTableWidgetItemNode::GetProperties() const
{
    QVariantMap properties;

    QTableWidget* parent = item_->tableWidget();
    QRect cellrect = parent->visualItemRect(item_);
    QRect r = QRect(parent->mapToGlobal(cellrect.topLeft()), cellrect.size());
    properties["globalRect"] = PackProperty(r);

    properties["text"] = SafePackProperty(PackProperty(item_->text()));
    properties["toolTip"] = SafePackProperty(PackProperty(item_->toolTip()));
    properties["icon"] = SafePackProperty(PackProperty(item_->icon()));
    properties["whatsThis"] = SafePackProperty(PackProperty(item_->whatsThis()));
    properties["row"] = SafePackProperty(PackProperty(item_->row()));
    properties["isSelected"] = SafePackProperty(PackProperty(item_->isSelected()));
    properties["column"] = SafePackProperty(PackProperty(item_->column()));

    return properties;
}

xpathselect::Node::Ptr QTableWidgetItemNode::GetParent() const
{
    return parent_;
}

std::string QTableWidgetItemNode::GetName() const
{
    return "QTableWidgetItem";
}

std::string QTableWidgetItemNode::GetPath() const
{
    return full_path_;
}

int32_t QTableWidgetItemNode::GetId() const
{
    return calculate_ap_id(static_cast<quint64>(reinterpret_cast<quintptr>(item_)));
}

bool QTableWidgetItemNode::MatchStringProperty(std::string const& name, std::string const& value) const
{
    return MatchProperty(GetProperties(), name, QString::fromStdString(value));
}

bool QTableWidgetItemNode::MatchIntegerProperty(std::string const& name, int32_t value) const
{
    if (name == "id")
        return value == GetId();

    return MatchProperty(GetProperties(), name, value);
}

bool QTableWidgetItemNode::MatchBooleanProperty(std::string const& name, bool value) const
{
    return MatchProperty(GetProperties(), name, value);
}

xpathselect::NodeVector QTableWidgetItemNode::Children() const
{
    // Doesn't have any children.
    xpathselect::NodeVector children;
    return children;
}

// QTreeWidgetItemNode
QTreeWidgetItemNode::QTreeWidgetItemNode(QTreeWidgetItem *item, DBusNode::Ptr parent)
    : item_(item)
    , parent_(parent)
{
    std::string parent_path = parent ? parent->GetPath() : "";
    full_path_ = parent_path + "/" + GetName();
}

NodeIntrospectionData QTreeWidgetItemNode::GetIntrospectionData() const
{
    NodeIntrospectionData data;
    data.object_path = QString::fromStdString(GetPath());
    data.state = GetProperties();
    data.state["id"] = PackProperty(GetId());
    return data;
}

QVariantMap QTreeWidgetItemNode::GetProperties() const
{
    QVariantMap properties;
    QTreeWidget* parent = item_->treeWidget();
    QRect cellrect = parent->visualItemRect(item_);
    QRect r = QRect(parent->viewport()->mapToGlobal(cellrect.topLeft()), cellrect.size());
    properties["globalRect"] = PackProperty(r);

    properties["text"] = SafePackProperty(item_->text(0));
    properties["columns"] = SafePackProperty(item_->columnCount());
    properties["checkState"] = SafePackProperty(item_->checkState(0));

    properties["isDisabled"] = SafePackProperty(item_->isDisabled());
    properties["isExpanded"] = SafePackProperty(item_->isExpanded());
    properties["isFirstColumnSpanned"] = SafePackProperty(item_->isFirstColumnSpanned());
    properties["isHidden"] = SafePackProperty(item_->isHidden());
    properties["isSelected"] = SafePackProperty(item_->isSelected());

    return properties;
}

xpathselect::Node::Ptr QTreeWidgetItemNode::GetParent() const
{
    return parent_;
}

std::string QTreeWidgetItemNode::GetName() const
{
    return "QTreeWidgetItem";
}

std::string QTreeWidgetItemNode::GetPath() const
{
    return full_path_;
}

int32_t QTreeWidgetItemNode::GetId() const
{
    return calculate_ap_id(static_cast<quint64>(reinterpret_cast<quintptr>(item_)));
}

bool QTreeWidgetItemNode::MatchStringProperty(std::string const& name, std::string const& value) const
{
    return MatchProperty(GetProperties(), name, QString::fromStdString(value));
}

bool QTreeWidgetItemNode::MatchIntegerProperty(std::string const& name, int32_t value) const
{
    if (name == "id")
        return value == GetId();

    return MatchProperty(GetProperties(), name, value);
}

bool QTreeWidgetItemNode::MatchBooleanProperty(std::string const& name, bool value) const
{
    return MatchProperty(GetProperties(), name, value);
}

xpathselect::NodeVector QTreeWidgetItemNode::Children() const
{
    xpathselect::NodeVector children;

    for(int i=0; i < item_->childCount(); ++i) {
        children.push_back(
            std::make_shared<QTreeWidgetItemNode>(item_->child(i),shared_from_this())
            );
    }

    return children;
}
