#include "GenericWidget.h"
#include <src/models/ValuesProxyModel.h>
#include <src/hkx/ItemsDelegate.h>

#include <QSpacerItem>
#include <QtWidgets/QLabel>
#include <QTableView>
#include <QLineEdit>
#include <QHeaderView>
#include <QScrollBar>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QToolButton>

using namespace ckcmd;
using namespace ckcmd::HKX;

GenericWidget::GenericWidget(ckcmd::HKX::ProjectModel& model, QWidget* parent) :
	ModelWidget(model, parent)
{
	this->resize(510, 372);
	verticalLayout = new QVBoxLayout(this);
	verticalLayout->setAlignment(Qt::AlignTop);
}

void clearLayout(QLayout* layout, bool deleteWidgets = true)
{
	while (QLayoutItem* item = layout->takeAt(0))
	{
		QWidget* widget;
		if ((deleteWidgets)
			&& (widget = item->widget())) {
			delete widget;
		}
		if (QLayout* childLayout = item->layout()) {
			clearLayout(childLayout, deleteWidgets);
		}
		delete item;
	}
}

void verticalResizeTableViewToContents(QTableView* tableView, bool min_row_value, bool forceScroll)
{
	int rowTotalHeight = 0;

	// Rows height
	int count = tableView->verticalHeader()->count();	
	for (int i = 0; i < count; ++i) {
		// 2018-03 edit: only account for row if it is visible
		if (!tableView->verticalHeader()->isSectionHidden(i)) {
			rowTotalHeight += tableView->verticalHeader()->sectionSize(i);
		}
	}
	if (count == 0 && min_row_value)
		rowTotalHeight = 23;

	// Check for scrollbar visibility


	if (tableView->horizontalScrollBar()->isVisible() || forceScroll)
	{
		if (forceScroll)
			rowTotalHeight += 23;
		else
			rowTotalHeight += tableView->horizontalScrollBar()->height();
	}

	// Check for header visibility
	if (!tableView->horizontalHeader()->isHidden())
	{
		rowTotalHeight += 23 + 3;//tableView->horizontalHeader()->height() + 3;
	}
	tableView->setMinimumHeight(rowTotalHeight);
	tableView->setMaximumHeight(rowTotalHeight);
}

void GenericWidget::makeFieldWidget
(
	const QString& labelText, 
	const std::vector<size_t>& rows, 
	const std::vector<QString>& columnLabels,
	bool isArray
)
{
	QLabel* label = new QLabel(this);
	ValuesProxyModel* editModel = new ValuesProxyModel(&_model, rows, 1, _index, this);
	QTableView* editor = new QTableView(this);
	label->setText(labelText);
	label->setMaximumHeight(23);
	if (isArray)
	{
		QHBoxLayout* hlayout = new QHBoxLayout(this);

		hlayout->addWidget(label);

		QToolButton* setAddButton;
		setAddButton = new QToolButton(this);
		setAddButton->setObjectName(QString::fromUtf8("setAddButton"));
		setAddButton->setText("+");

		int first_row = rows.at(0);

		QToolButton* setRemoveButton;
		setRemoveButton = new QToolButton(this);
		setRemoveButton->setObjectName(QString::fromUtf8("setRemoveButton"));
		setRemoveButton->setText("-");

		int actual_values = editModel->rowCount();
		if (actual_values <= 0)
			setRemoveButton->setEnabled(false);

		connect(setAddButton, &QToolButton::clicked, [editor, editModel, first_row, setRemoveButton]()
		{
			int new_row = editModel->rowCount();
			editModel->insertRow(new_row);
			editor->resizeRowsToContents();
			editor->resizeColumnsToContents();
			int check = editModel->rowCount();
			verticalResizeTableViewToContents(editor, false, false);
			setRemoveButton->setEnabled(true);
		});

		connect(setRemoveButton, &QToolButton::clicked, [editor, editModel, first_row, setRemoveButton]()
		{
			auto index = editor->selectionModel()->currentIndex();
			int row = editModel->rowCount() - 1;
			if (index.isValid())
			{
				row = index.row();
			}
			int new_row = editModel->rowCount();
			editModel->removeRow(row);
			editor->resizeRowsToContents();
			editor->resizeColumnsToContents();
			int check = editModel->rowCount();
			verticalResizeTableViewToContents(editor, false, false);
			if (check <= 0)
				setRemoveButton->setEnabled(false);
		});


		hlayout->addWidget(setAddButton);
		hlayout->addWidget(setRemoveButton);

		verticalLayout->addLayout(hlayout);
	}
	else {
		verticalLayout->addWidget(label);
	}


	editor->horizontalHeader()->setVisible(false);
	editor->horizontalHeader()->setStretchLastSection(true);
	editor->verticalHeader()->setMinimumSectionSize(20);
	editor->verticalHeader()->setDefaultSectionSize(20);
	editor->verticalHeader()->setVisible(false);
	editor->setShowGrid(false);
	editor->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
	if (rows.size() > 1)
	{
		for (size_t i = 0; i < columnLabels.size(); ++i)
			editModel->setHeaderData(i, Qt::Horizontal, columnLabels[i]);
	}
	editor->setItemDelegate(new ItemsDelegate(*editModel, this));
	editor->setModel(editModel);
	editor->resizeRowsToContents();
	if (rows.size() > 1)
	{
		editor->setSelectionMode(QAbstractItemView::SingleSelection);
		editor->horizontalHeader()->setVisible(true);
		editor->resizeColumnsToContents();

	}
	else {
		editor->setSelectionMode(QAbstractItemView::NoSelection);
		editor->horizontalHeader()->setVisible(false);

	}
	verticalLayout->addWidget(editor);

	bool forceScroll = columnLabels.size() > 5;

	if (rows.size() > 1)
		verticalResizeTableViewToContents(editor, !isArray, forceScroll);
	else
		verticalResizeTableViewToContents(editor, true, forceScroll);
}

void GenericWidget::OnIndexSelected()
{
	buildReflectionTable();
	clearLayout(verticalLayout);

	QString last_group_name;
	std::vector<size_t> last_group;
	std::vector<QString> last_group_fields;
	bool last_group_isArray = false;

	bool isArray = false;
	for (const auto& member : _members)
	{
		int row = member.second.first;
		QModelIndex row_index = _model.index(row, 0, _index);
		auto type = _model.rowType(row_index);
		if (type._class != nullptr)
			continue;
		if (std::get<0>(member) == "name")
			continue;

		isArray = _model.isArray(row_index);

		auto dot_index = std::get<0>(member).indexOf(".");
		if (dot_index != -1)
		{
			//this is an object;
			last_group.push_back(member.second.first);
			last_group_name = member.first.left(dot_index);
			last_group_fields.push_back(member.first.mid(dot_index));
			last_group_isArray = isArray;
			continue;
		}
		else {
			if (!last_group.empty())
			{
				makeFieldWidget(last_group_name, last_group, last_group_fields, last_group_isArray);
				last_group.clear();
				last_group_fields.clear();
				last_group_isArray = false;
			}
			makeFieldWidget(member.first, { member.second.first }, last_group_fields, isArray);
		}
	}
	if (!last_group.empty())
	{
		makeFieldWidget(last_group_name, last_group, last_group_fields, last_group_isArray);
		last_group.clear();
		last_group_fields.clear();
	}
}