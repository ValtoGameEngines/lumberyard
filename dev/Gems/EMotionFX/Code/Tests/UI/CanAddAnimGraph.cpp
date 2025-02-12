/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/

#include <gtest/gtest.h>

#include <QPushButton>
#include <QAction>
#include <QtTest>

#include <Tests/UI/UIFixture.h>
#include <EMotionFX/Source/AnimGraphManager.h>
#include <EMotionStudio/EMStudioSDK/Source/EMStudioManager.h>
#include <EMotionStudio/Plugins/StandardPlugins/Source/AnimGraph/AnimGraphPlugin.h>
#include <EMotionStudio/Plugins/StandardPlugins/Source/AnimGraph/BlendGraphViewWidget.h>

namespace EMotionFX
{
    TEST_F(UIFixture, CanAddAnimGraph)
    {
        RecordProperty("test_case_id", "C953542");

        EMStudio::GetMainWindow()->ApplicationModeChanged("AnimGraph");

        auto animGraphPlugin = static_cast<EMStudio::AnimGraphPlugin*>(EMStudio::GetPluginManager()->FindActivePlugin(EMStudio::AnimGraphPlugin::CLASS_ID));
        ASSERT_TRUE(animGraphPlugin) << "Anim graph plugin not found.";
        ASSERT_FALSE(animGraphPlugin->GetActiveAnimGraph()) << "No anim graph should be activated.";
        ASSERT_EQ(0, EMotionFX::GetAnimGraphManager().GetNumAnimGraphs()) << "Anim graph manager should contain 0 anim graph.";

        auto addAnimGraphButton = animGraphPlugin->GetViewWidget()->findChild<QPushButton*>("EMFX.BlendGraphViewWidget.NewButton");
        ASSERT_TRUE(addAnimGraphButton) << "Add Anim graph button not found.";

        QTest::mouseClick(addAnimGraphButton, Qt::LeftButton);

        AnimGraph* newGraph = animGraphPlugin->GetActiveAnimGraph();
        // The empty graph should contain one node (the root statemachine).
        ASSERT_TRUE(newGraph && newGraph->GetNumNodes() == 1) << "An empty anim graph should be activated.";
        ASSERT_EQ(1, EMotionFX::GetAnimGraphManager().GetNumAnimGraphs()) << "Anim graph manager should contain 1 anim graph.";

        QTest::mouseClick(addAnimGraphButton, Qt::LeftButton);
        ASSERT_EQ(2, EMotionFX::GetAnimGraphManager().GetNumAnimGraphs()) << "Anim graph manager should contain 2 anim graphs.";
        AnimGraph* newGraph2 = animGraphPlugin->GetActiveAnimGraph();
        ASSERT_NE(newGraph, newGraph2) << "After the second click, the active graph should change.";

        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
} // namespace EMotionFX
