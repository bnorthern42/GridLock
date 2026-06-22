#pragma once
#include <QObject>

class IBackendCoordinator : public QObject {
    Q_OBJECT
public:
    explicit IBackendCoordinator(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IBackendCoordinator() = default;

    // In DAP, "threadId" is universally used for execution context.
    // For our MPI debugger, we map Rank 0 to Thread 1, Rank 1 to Thread 2, etc.
    virtual void stepOver(int threadId) = 0;
    virtual void stepInto(int threadId) = 0;
    virtual void continueExecution(int threadId) = 0;
    virtual void pauseExecution(int threadId) = 0;
    virtual void evaluateExpression(int rankId, const QString& expression) = 0;

signals:
    void locationChanged(int rankId, const QString& file, int line);
    void expressionEvaluated(int rankId, const QString& expr, const QString& result);
};
