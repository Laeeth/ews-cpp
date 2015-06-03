#include "fixture.hpp"

#include <string>
#include <vector>
#include <algorithm>
#include <iterator>
#include <utility>

namespace tests
{
    class TaskTest : public ServiceFixture {};

    // TODO: test does not need to be in a fixture that can talk to the server
#pragma warning(suppress: 6262)
    TEST_F(TaskTest, FromXmlElement)
    {
        using xml_document = rapidxml::xml_document<>;

        // slang: 2013 SP1, not all properties included
        const auto xml = std::string(R"(
            <t:Task
            xmlns:t="http://schemas.microsoft.com/exchange/services/2006/types">
                <t:ItemId Id="abcde" ChangeKey="edcba"/>
                <t:ParentFolderId Id="qwertz" ChangeKey="ztrewq"/>
                <t:ItemClass>IPM.Task</t:ItemClass>
                <t:Subject>Write poem</t:Subject>
                <t:Sensitivity>Normal</t:Sensitivity>
                <t:Body BodyType="Text" IsTruncated="false"/>
                <t:DateTimeReceived>2015-02-09T13:00:11Z</t:DateTimeReceived>
                <t:Size>962</t:Size>
                <t:Importance>Normal</t:Importance>
                <t:IsSubmitted>false</t:IsSubmitted>
                <t:IsDraft>false</t:IsDraft>
                <t:IsFromMe>false</t:IsFromMe>
                <t:IsResend>false</t:IsResend>
                <t:IsUnmodified>false</t:IsUnmodified>
                <t:DateTimeSent>2015-02-09T13:00:11Z</t:DateTimeSent>
                <t:DateTimeCreated>2015-02-09T13:00:11Z</t:DateTimeCreated>
                <t:DisplayCc/>
                <t:DisplayTo/>
                <t:HasAttachments>false</t:HasAttachments>
                <t:Culture>en-US</t:Culture>
                <t:EffectiveRights>
                        <t:CreateAssociated>false</t:CreateAssociated>
                        <t:CreateContents>false</t:CreateContents>
                        <t:CreateHierarchy>false</t:CreateHierarchy>
                        <t:Delete>true</t:Delete>
                        <t:Modify>true</t:Modify>
                        <t:Read>true</t:Read>
                        <t:ViewPrivateItems>true</t:ViewPrivateItems>
                </t:EffectiveRights>
                <t:LastModifiedName>Kwaltz</t:LastModifiedName>
                <t:LastModifiedTime>2015-02-09T13:00:11Z</t:LastModifiedTime>
                <t:IsAssociated>false</t:IsAssociated>
                <t:Flag>
                        <t:FlagStatus>NotFlagged</t:FlagStatus>
                </t:Flag>
                <t:InstanceKey>AQAAAAAAARMBAAAAG4AqWQAAAAA=</t:InstanceKey>
                <t:EntityExtractionResult/>
                <t:ChangeCount>1</t:ChangeCount>
                <t:IsComplete>false</t:IsComplete>
                <t:IsRecurring>false</t:IsRecurring>
                <t:PercentComplete>0</t:PercentComplete>
                <t:Status>NotStarted</t:Status>
                <t:StatusDescription>Not Started</t:StatusDescription>
            </t:Task>)");
        std::vector<char> buf;
        std::copy(begin(xml), end(xml), std::back_inserter(buf));
        buf.push_back('\0');
        xml_document doc;
        doc.parse<0>(&buf[0]);
        auto node = doc.first_node();
        auto t = ews::task::from_xml_element(*node);

        EXPECT_STREQ("abcde", t.get_item_id().id().c_str());
        EXPECT_STREQ("edcba", t.get_item_id().change_key().c_str());
        EXPECT_STREQ("Write poem", t.get_subject().c_str());
    }

    TEST_F(TaskTest, GetTaskWithInvalidIdThrows)
    {
        auto invalid_id = ews::item_id();
        EXPECT_THROW(service().get_task(invalid_id), ews::exchange_error);
    }

    TEST_F(TaskTest, GetTaskWithInvalidIdExceptionResponse)
    {
        auto invalid_id = ews::item_id();
        try
        {
            service().get_task(invalid_id);
            FAIL() << "Expected an exception";
        }
        catch (ews::exchange_error& exc)
        {
            EXPECT_EQ(ews::response_code::error_invalid_id_empty, exc.code());
            EXPECT_STREQ("ErrorInvalidIdEmpty", exc.what());
        }
    }

    TEST_F(TaskTest, CreateAndDelete)
    {
        auto start_time = ews::date_time("2015-01-17T12:00:00Z");
        auto end_time   = ews::date_time("2015-01-17T12:30:00Z");
        auto task = ews::task();
        task.set_subject("Something really important to do");
        task.set_body(ews::body("Some descriptive body text"));
        task.set_start_date(start_time);
        task.set_due_date(end_time);
        task.set_reminder_enabled(true);
        task.set_reminder_due_by(start_time);
        const auto item_id = service().create_item(task);

        auto created_task = service().get_task(item_id);
        // Check properties
        EXPECT_STREQ("Something really important to do",
                created_task.get_subject().c_str());
        EXPECT_EQ(start_time, created_task.get_start_date());
        EXPECT_EQ(end_time, created_task.get_due_date());
        EXPECT_TRUE(created_task.is_reminder_enabled());
        EXPECT_EQ(start_time, created_task.get_reminder_due_by());

        ASSERT_NO_THROW(
        {
            service().delete_task(
                    std::move(created_task), // Sink argument
                    ews::delete_type::hard_delete,
                    ews::affected_task_occurrences::all_occurrences);
        });
        EXPECT_STREQ("", created_task.get_subject().c_str());

        // Check if it is still in store
        EXPECT_THROW(service().get_task(item_id), ews::exchange_error);
    }

    TEST_F(TaskTest, FindTasks)
    {
        ews::task_property_path task;
        ews::distinguished_folder_id folder = ews::standard_folder::tasks;
        const auto initial_count = service().find_item(folder,
            ews::is_equal_to(task.is_complete, false)).size();

        auto start_time = ews::date_time("2015-05-29T17:00:00Z");
        auto end_time   = ews::date_time("2015-05-29T17:30:00Z");
        auto t = ews::task();
        t.set_subject("Feed the cat");
        t.set_body(ews::body("And don't forget to buy some Whiskas"));
        t.set_start_date(start_time);
        t.set_due_date(end_time);
        const auto item_id = service().create_item(t);
        t = service().get_task(item_id);
        ews::internal::on_scope_exit delete_item([&]
        {
            service().delete_task(std::move(t));
        });
        auto ids = service().find_item(folder,
                                       ews::is_equal_to(task.is_complete, false));
        EXPECT_EQ(initial_count + 1, ids.size());
    }
}