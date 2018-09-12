using System.Collections;
using System.Collections.Generic;
using UnityEngine;


/// <summary>
///  Скрипт, управляющий офицером 
/// НЕ ВЫДЕЛЕНА ПАМЯТЬ ПОД КОНТЕЙНЕРЫ С НИМ
/// </summary>
public class offiser_brain : FarOficerBrains {


    /// <summary>
    /// Влияет на управление солдатами
    /// </summary>
    public float charisma;

    /// <summary>
    /// Влияет на скорость принятия решений
    /// </summary>
    public float tacticMind;

    /// <summary>
    /// Какого размера может взять под командование 
    /// </summary>
    public float LeadSkill;

    /// Типы командира:
    /// Офицер - 1
    /// Знаменосец - 3
    /// Музыкант - 4
    /// Адьютант - 0
    /// Плюс старшиекомнадные чины
    public int IdTypeOfficers;

    public int IdOfficer;

    /// <summary>
    /// Дистанция, на которой может управлять
    /// </summary>
    public float LeadDist;

    public float moral;

    /// <summary>
    /// Сколько периодов провел с полком
    /// </summary>
    public int TurnsWithReg;

    /// <summary>
    /// Полк, в котором состоит 
    /// </summary>
    public int RegimentNum;

    /// <summary>
    ///Бонус к прохождению теста на панику управляемым полком
    /// </summary>
    public float BuffPanic;



    /// <summary>
    ///  Пересчитывает бонусы, которые дает офицер
    /// </summary>
    public void reCalc()
    {

    }


    void OnMouseDown()
    {
        if (GameStatus.statesChoosen == true)
        {

            GameStatus.UnitControl = true;
            platerControl = true;

            // Записываем положение камеры
            GameStatus.Cam1 = Camera.main.transform.position;
            GameStatus.Cam2 = Camera.main.transform.rotation;
            Debug.Log(transform.position);
            Vector3 np = transform.position - transform.forward * 3 + new Vector3(0, 2, 0);
            Debug.Log(np);
            Camera.main.transform.position = np;
            Camera.main.transform.rotation = transform.rotation;

        }
    }


    void Update()
    {
        if (platerControl == true)
        {


            if (false == true)
            {
                /// Когда юнит под управлением человека



                if (Input.GetKey(KeyCode.UpArrow))
                    transform.Translate(Vector3.forward * 2 * speed * Time.deltaTime);


                if (Input.GetKey(KeyCode.DownArrow))
                    transform.Translate(-Vector3.forward * 2 * speed * Time.deltaTime);

                if (Input.GetKey(KeyCode.LeftArrow))
                    transform.Rotate(Vector3.up, -28f * Time.deltaTime);

                if (Input.GetKey(KeyCode.RightArrow))
                    transform.Rotate(Vector3.up, +28f * Time.deltaTime);

                if (RegimentNum != 99999)
                {
                    if (Input.GetKey(KeyCode.U))
                        mainStatic.regiment_stat[RegimentNum].orderForw = true;

                    if (Input.GetKey(KeyCode.J))
                        mainStatic.regiment_stat[RegimentNum].orderBack = true;

                    if (Input.GetKey(KeyCode.H))
                        mainStatic.regiment_stat[RegimentNum].orderRotL = true;

                    if (Input.GetKey(KeyCode.K))
                        mainStatic.regiment_stat[RegimentNum].orderRotW = true;

                    if (Input.GetKey(KeyCode.T))
                    {
                        mainStatic.regiment_stat[RegimentNum].archTotewer = true;
                        mainStatic.regiment_stat[RegimentNum].archAlone = false;
                    }

                    if (Input.GetKey(KeyCode.G))
                    {
                        mainStatic.regiment_stat[RegimentNum].archAlone = true;
                        mainStatic.regiment_stat[RegimentNum].archTotewer = false;
                    }

                }

                // Сдвигаем камеру к юниту
                Vector3 tt = transform.position; tt.y += 2;

                //Debug.Log(transform.position);
                Vector3 np = transform.position - transform.forward * 2 + new Vector3(0, 2, 0);
                //Debug.Log(np);

                Camera.main.transform.position = np;
                Camera.main.transform.rotation = transform.rotation;


                if (GameStatus.ControlReset == true)
                {
                    Camera.main.transform.position = GameStatus.Cam1;
                    Camera.main.transform.rotation = GameStatus.Cam2;
                    platerControl = false;
                    GameStatus.statesChoosen = false;
                    GameStatus.UnitControl = false;
                    GameStatus.ControlReset = false;
                }


                // Режим при проверке х-рэй

                Ray ray1 = new Ray(transform.position, transform.forward);//forw );
                Ray ray2 = new Ray(transform.position, mainStatic.myRot(transform.forward, 3.1415f));
                Ray ray3 = new Ray(transform.position, mainStatic.myRot(transform.forward, 3.1415f / 4 * 7));
                Ray ray4 = new Ray(transform.position, mainStatic.myRot(transform.forward, 3.1415f / 4));

                Ray ray5 = new Ray(transform.position, mainStatic.myRot(transform.forward, 3.1415f * 5 / 4));

                Ray ray6 = new Ray(transform.position, mainStatic.myRot(transform.forward, 3.1415f * 3 / 4));


                RaycastHit hit1; RaycastHit hit2; RaycastHit hit3; RaycastHit hit4; RaycastHit hit5; RaycastHit hit6;

                bool temp = Physics.Raycast(ray1, out hit1, 4.0f);
                if (temp == true)
                {
                    GameStatus.fw = GameStatus.fl = GameStatus.bw = GameStatus.bl = GameStatus.bk = false;
                    GameStatus.fr = true;
                }

                temp = Physics.Raycast(ray2, out hit2, 4.0f);
                if (temp == true)
                {
                    GameStatus.fr = GameStatus.fw = GameStatus.fl = GameStatus.bw = GameStatus.bl = false;
                    GameStatus.bk = true;
                }


                temp = Physics.Raycast(ray3, out hit3, 4.0f);
                if (Physics.Raycast(ray3, out hit3, 4.0f) == true)
                {
                    GameStatus.fr = GameStatus.fw = GameStatus.bw = GameStatus.bl = GameStatus.bk = false;
                    GameStatus.fl = true;
                }

                temp = Physics.Raycast(ray4, out hit4, 4.0f);
                if (Physics.Raycast(ray4, out hit4, 4.0f) == true)
                {
                    GameStatus.fr = GameStatus.fl = GameStatus.bw = GameStatus.bl = GameStatus.bk = false;
                    GameStatus.fw = true;
                }

                if (Physics.Raycast(ray5, out hit5, 4.0f) == true)
                {
                    GameStatus.fr = GameStatus.fw = GameStatus.fl = GameStatus.bw = GameStatus.bk = false;
                    GameStatus.bl = true;
                }


                if (Physics.Raycast(ray6, out hit6, 4.0f) == true)
                {
                    GameStatus.fr = GameStatus.fw = GameStatus.fl = GameStatus.bl = GameStatus.bk = false;
                    GameStatus.bw = true;
                }


                if (Input.GetKey(KeyCode.Q))
                {
                    if (GameStatus.fr == true && hit1.transform.GetComponent<UnitSoldiers>().command == command)
                    {
                        RegimentNum = hit1.transform.GetComponent<UnitSoldiers>().numReg;
                        //RegimentNum = hit3.transform.GetComponent<UnitSoldiers>().numReg;
                        //RegimentNum = hit4.transform.GetComponent<UnitSoldiers>().numReg;
                    }
                }

            }
        }
        else
        {

            if ((-transform.position + new_position).magnitude > 0.2)
            {
                Vector3 tmp = -transform.position + new_position;
                //tmp.y = 0.0f;
                transform.position = transform.position + tmp.normalized * Time.deltaTime * 2.0f;
                transform.position = new Vector3(transform.position.x, 0f, transform.position.z);
            }
            else
            {
                transform.position = new_position;
            }

            transform.rotation = Quaternion.Euler(0, mainStatic.regiment_stat[numReg].angle * 53, 0);
        }
    }


}
