// -------------------------------------------------------
// Добавить этот метод в project_indexer.cpp
// (в конец файла, после существующих методов)
// -------------------------------------------------------

// QStringList ProjectIndexer::recentFiles(int n) const
//
// Возвращает до N путей к файлам отсортированных по
// lastModified убыванию (самые свежие — первые).
// Используется Brain::captureSnapshot для контекста.

QStringList ProjectIndexer::recentFiles(int n) const
{
    if (m_files.isEmpty()) return {};

    // Собираем пары (lastModified, filePath) и сортируем
    QVector<QPair<QDateTime, QString>> dated;
    dated.reserve(m_files.size());
    for (auto it = m_files.begin(); it != m_files.end(); ++it) {
        dated.append({it.value().lastModified, it.value().filePath});
    }

    std::sort(dated.begin(), dated.end(),
              [](const QPair<QDateTime, QString>& a,
                 const QPair<QDateTime, QString>& b) {
                  return a.first > b.first;  // убывание
              });

    QStringList result;
    const int count = qMin(n, static_cast<int>(dated.size()));
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        result.append(dated[i].second);
    }
    return result;
}
