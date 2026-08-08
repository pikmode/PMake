#pragma once

#include <QByteArray>
#include <QHash>
#include <QVector>

namespace pmake::util {

// Пул строк: хранит QByteArray один раз, возвращает индекс.
class StringPool {
public:
    static StringPool& instance() {
        static StringPool pool;
        return pool;
    }

    int intern(const QByteArray& str) {
        auto it = map_.find(str);
        if (it != map_.end())
            return *it;
        int id = strings_.size();
        strings_.append(str);
        map_.insert(str, id);
        return id;
    }

    int intern(const char* str) {
        return intern(QByteArray(str));
    }

    const QByteArray& get(int id) const {
        return strings_.at(id);
    }

    int size() const { return strings_.size(); }

    void clear() {
        strings_.clear();
        map_.clear();
    }

private:
    StringPool() = default;
    QVector<QByteArray> strings_;
    QHash<QByteArray, int> map_;
};

} // namespace pmake::util