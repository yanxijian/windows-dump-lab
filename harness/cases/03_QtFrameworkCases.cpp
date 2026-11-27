// Cases 13-18: Qt framework lifetime, thread affinity and model-pointer faults.

#include "core/CaseRegistry.h"
#include "core/DumpHelper.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <thread>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QMetaObject>
#include <QModelIndex>
#include <QPixmap>
#include <QObject>
#include <QTimer>
#include <QVariant>
#include <QWidget>

namespace
{
    // ---------------------------------------------------------------- case 13
    // deleteLater() + a singleShot lambda that captured [this] without a
    // receiver context: the deferred delete runs first, the lambda still fires.
    class Victim : public QObject
    {
    public:
        int payload[4]{};

        ~Victim() override
        {
            std::printf("[case 13] Victim destroyed\n");
            std::fflush(stdout);
        }
    };

    void Case13()
    {
        DumpHelper::StartWatchdog("", 1500, false); // backstop if the UAF does not fault

        Victim* victim = new Victim();
        victim->deleteLater();

        QTimer::singleShot(40, [] {
            // Recycle the freed Victim block and smash it, so the vtable is garbage.
            for (int i = 0; i < 64; ++i)
            {
                void* junk = new char[sizeof(Victim)];
                std::memset(junk, 0xDD, sizeof(Victim));
            }
        });

        QTimer::singleShot(80, [victim] {
            const QMetaObject* meta = victim->metaObject(); // @fault (virtual call on freed object)
            std::printf("[case 13] metaObject=%p\n", static_cast<const void*>(meta));
        });

        QCoreApplication::exec();
    }

    // ---------------------------------------------------------------- case 14
    // Child QObject dies with its parent; the raw pointer is reused without QPointer.
    class ChildNode : public QObject
    {
    public:
        int payload[4]{};
    };

    void Case14()
    {
        DumpHelper::StartWatchdog("", 1500, false);

        QObject* parent = new QObject();
        ChildNode* child = new ChildNode();
        child->setParent(parent);

        delete parent; // destroys the child as well
        for (int i = 0; i < 32; ++i)
        {
            void* junk = new char[sizeof(ChildNode)];
            std::memset(junk, 0xDD, sizeof(ChildNode));
        }

        child->setObjectName(QStringLiteral("zombie")); // @fault (child freed, d_ptr smashed)
        std::printf("[case 14] setObjectName returned without fault\n");
        Sleep(INFINITE);
    }

    // ---------------------------------------------------------------- case 15
    // QWidget created and driven from a worker thread while the GUI thread owns
    // the application object.
    void Case15()
    {
        DumpHelper::StartWatchdog("", 3000, false);

        QWidget* mainWindow = new QWidget();
        mainWindow->resize(320, 200);
        mainWindow->show();

        std::thread worker([mainWindow] {
            QWidget* rogue = new QWidget(); // QWidget off the GUI thread
            rogue->resize(120, 80);
            rogue->show();
            rogue->winId(); // forces native window creation on the worker thread

            mainWindow->setWindowTitle(QStringLiteral("touched from worker")); // @fault
            QPixmap snapshot(rogue->size());
            rogue->render(&snapshot);
        });
        worker.join();

        Sleep(INFINITE);
    }

    // ---------------------------------------------------------------- case 17
    // The base destructor runs after ~DocumentImpl, so the vtable is already back
    // to DocumentBase; dispatching through a member pointer therefore lands on
    // the pure-virtual slot (_purecall).
    class DocumentBase : public QObject
    {
    public:
        ~DocumentBase() override
        {
            void (DocumentBase::*notify)() = &DocumentBase::onDestroyed;
            (this->*notify)(); // @fault (_purecall during destruction)
        }

        virtual void onDestroyed() = 0;
    };

    class DocumentImpl : public DocumentBase
    {
    public:
        void onDestroyed() override {}
    };

    void Case17()
    {
        DocumentBase* document = new DocumentImpl();
        delete document;
    }

    // ---------------------------------------------------------------- case 18
    // A model hands out QModelIndex::internalPointer() for a node it later frees.
    struct ModelNode
    {
        virtual ~ModelNode() = default;
        virtual int value() const { return 1; }
        int payload[4]{};
    };

    class PointerModel : public QAbstractItemModel
    {
    public:
        explicit PointerModel(ModelNode* root) : m_root(root) {}

        QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override
        {
            (void)column;
            (void)parent;
            return createIndex(row, 0, m_root);
        }
        QModelIndex parent(const QModelIndex&) const override { return QModelIndex(); }
        int rowCount(const QModelIndex&) const override { return 1; }
        int columnCount(const QModelIndex&) const override { return 1; }
        QVariant data(const QModelIndex&, int) const override { return QVariant(); }

    private:
        ModelNode* m_root;
    };

    void Case18()
    {
        DumpHelper::StartWatchdog("", 1500, false);

        ModelNode* node = new ModelNode();
        PointerModel model(node);
        QModelIndex index = model.index(0, 0);

        ModelNode* pointer = static_cast<ModelNode*>(index.internalPointer());
        delete pointer; // the model still stores the dangling pointer
        for (int i = 0; i < 32; ++i)
        {
            void* junk = new char[sizeof(ModelNode)];
            std::memset(junk, 0xDD, sizeof(ModelNode));
        }

        volatile int value = pointer->value(); // @fault (virtual call on freed node)
        (void)value;
        Sleep(INFINITE);
    }
}

// ---------------------------------------------------------------- case 16
// Two objects bounce the same signal back and forth with direct connections.
// Declared at file scope on purpose: moc needs a named class for the .moc file.
class LoopNode : public QObject
{
    Q_OBJECT
public:
    void kick() { emit tick(); }

signals:
    void tick();

public slots:
    void onTick()
    {
        emit tick(); // @fault (unbounded recursion -> 0xC00000FD)
    }
};

void Case16()
{
    LoopNode node;
    QObject::connect(&node, &LoopNode::tick, &node, &LoopNode::onTick);
    node.kick();
}

REGISTER_CASE(13, "13_qt_deleteLater_lambda_uaf", true, &Case13);
REGISTER_CASE(14, "14_qt_parent_tree_uaf", true, &Case14);
REGISTER_CASE(15, "15_qt_cross_thread_ui", true, &Case15);
REGISTER_CASE(16, "16_qt_signal_loop_overflow", true, &Case16);
REGISTER_CASE(17, "17_qt_slot_during_destruction", true, &Case17);
REGISTER_CASE(18, "18_qt_invalid_model_index", true, &Case18);

#include "03_QtFrameworkCases.moc"
