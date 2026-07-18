#include "DzPythonBridgePane.h"
#include "common_version.h"

#if DAZ_SDK_MAJOR_VERSION >= 6
#include <QtWidgets/qboxlayout.h>
#include <QtWidgets/qframe.h>
#include <QtWidgets/qlabel.h>
#else
#include <QtGui/qboxlayout.h>
#include <QtGui/qframe.h>
#include <QtGui/qlabel.h>
#endif

DzPythonBridgePane::DzPythonBridgePane()
	: DzPane("Daz Python Bridge")
{
	QLabel* titleLabel = new QLabel(
		QString("Daz Python Bridge  v%1").arg(DPB_VERSION_STR), this);
	titleLabel->setStyleSheet(
		"QLabel { font-size: 11pt; font-weight: bold; padding: 4px 0px; }");
	titleLabel->setAlignment(Qt::AlignCenter);

	QFrame* titleSep = new QFrame(this);
	titleSep->setFrameShape(QFrame::HLine);
	titleSep->setFrameShadow(QFrame::Sunken);

	// Empty shell — sop.6 populates this with the plugin status list and
	// start/stop/restart/enable/disable controls.
	m_pContentContainer = new QWidget(this);
	new QVBoxLayout(m_pContentContainer);

	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(4, 4, 4, 4);
	mainLayout->addWidget(titleLabel);
	mainLayout->addWidget(titleSep);
	mainLayout->addWidget(m_pContentContainer);
	mainLayout->addStretch();
	setLayout(mainLayout);
}

#include "moc_DzPythonBridgePane.cpp"
