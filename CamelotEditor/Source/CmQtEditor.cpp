#include "CmQtEditor.h"
#include "CmQtDockOverlayWidget.h"
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QStatusBar>
#include <QtCore/QLocale>
#include <QtWidgets/QApplication>
#include <boost/bind.hpp>
#include "CmException.h"

namespace CamelotEditor
{
	QtEditor::QtEditor(QWidget *parent)
		: QMainWindow(parent)
	{
		setupUi();
	}

	QtEditor::~QtEditor()
	{

	}

	void QtEditor::setupUi()
	{
		resize(600, 400);

		mMenuBar = new QMenuBar(this);
		setMenuBar(mMenuBar);

		mMainToolBar = new QToolBar(this);
		addToolBar(mMainToolBar);

		mCentralWidget = new QWidget(this);
		setCentralWidget(mCentralWidget);

		mStatusBar = new QStatusBar(this);
		setStatusBar(mStatusBar);
		
		addMenuItemCallback("File", "Open project", boost::bind(&QtEditor::openProject, this));
		addMenuItemCallback("File", "Save project", boost::bind(&QtEditor::saveProject, this));
		addMenuItemSeparator("File");
		addMenuItemCallback("File", "Exit", boost::bind(&QtEditor::exitEditor, this));

		mDockOverlayWidget = new QtDockOverlayWidget(this);

		retranslateUi();
		setObjectNames();

		QMetaObject::connectSlotsByName(this);
	}

	void QtEditor::retranslateUi()
	{
		setProjectName("No project");
	}

	void QtEditor::setObjectNames()
	{
		mMenuBar->setObjectName(QStringLiteral("MenuBar"));
		mMainToolBar->setObjectName(QStringLiteral("MainToolBar"));
		mCentralWidget->setObjectName(QStringLiteral("CentralWidget"));
		mStatusBar->setObjectName(QStringLiteral("StatusBar"));
		mDockOverlayWidget->setObjectName(QStringLiteral("DockOverlayWidget"));
	}

	void QtEditor::setProjectName(const QString& name)
	{
		QString title = tr("Camelot Editor") + " - " + name;

		setWindowTitle(title);
	}

	QAction* QtEditor::addMenuItemCallback(const QString& menuCategory, const QString& itemName, boost::function<void()> callback)
	{
		QMenu* menu = findOrCreateMenu(menuCategory);

		QAction* newAction = menu->addAction(itemName);
		connect(newAction, &QAction::triggered, callback);

		return newAction;
	}

	void QtEditor::addMenuItemSeparator(const QString& menuCategory)
	{
		QMenu* menu = findOrCreateMenu(menuCategory);

		menu->addSeparator();
	}

	QMenu* QtEditor::findOrCreateMenu(const QString& name)
	{
		auto iterFind = mMenus.find(name);
		QMenu* menu = nullptr;

		if(iterFind == mMenus.end())
		{
			menu = new QMenu(mMenuBar);
			menu->setObjectName(name);
			menu->setTitle(name);
			mMenuBar->addAction(menu->menuAction());

			mMenus[name] = menu;

			return menu;
		}
		else
			return iterFind->second;
	}

	void QtEditor::openProject()
	{
		CM_EXCEPT(NotImplementedException, "Not implemented");
	}

	void QtEditor::saveProject()
	{
		CM_EXCEPT(NotImplementedException, "Not implemented");
	}

	void QtEditor::exitEditor()
	{
		exit(1);
	}
}
