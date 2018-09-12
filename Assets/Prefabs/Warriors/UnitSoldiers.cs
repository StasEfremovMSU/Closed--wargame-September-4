using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class UnitSoldiers : MonoBehaviour
{
    //// Тип солдата
    public bool archer;
    public bool spearman;
    /// <summary>
    ///  Номер созданного объекта
    /// </summary>
    public int gl_number;
    /// <summary>
    /// Номер полка в котором солдат
    /// </summary>
    public int numReg;
    /// <summary>
    /// Номер солдата в полку
    /// </summary>
    public int numSoldier;
    /// <summary>
    ///  К какой команде относится
    /// </summary>
    public int command;
    ///////////////////// Положение в полке \\\\\\\\\\\\\\\\\\
    /// <summary>
    /// Положение, относительно центра полка
    /// </summary>
    public Vector3 matrix_position;
    public int k1;
    public int k2;
    /// <summary>
    /// Переменная для перестроения
    /// </summary>
    public int tempForRebuilding;
    public Vector3 position;

    /////////////////// Стандартные параметры солдат \\\\\\\\\\\\\\\\\\\
    // Для ближнего боя
    public float attack = 20;
    public float defence = 4;
    /// <summary>
    /// Пробивная мощь оружия
    /// </summary>
    public float power = 1;
    public float speed = 2;
    /// <summary>
    ///  Тип солдата
    /// </summary>
    public int unitType;
    public int IdSoldiers;
    public string listItem;

    /// <summary>
    /// В какую точку идти не в бою
    /// </summary>
    public Vector3 new_position;
    /// <summary>
    /// Здесь анимация
    /// </summary>
    public Animator my_animator;
    /// <summary>
    /// В какую точку идти в бою
    /// </summary>
    public Vector3 nowTargetPosition;
    /// <summary>
    /// Находится ли под управлением игрока true - да
    /// </summary>
    public bool control;
    /// <summary>
    /// Находится ли в первой линии
    /// </summary>
    public bool FirstLine;

    public float angle;

    void Start()
    {
        my_animator = gameObject.GetComponent<Animator>();
        control = false;
    }

    public GameObject getObject()
    {
        return this.gameObject;
    }

    void Update()
    {
        if (control == false)
            if (mainStatic.regiment_stat[numReg].stateFighting == true)
            {
                moving_in_attack();
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

    private void moving_in_attack()
    {
        /// Если перед солдатом враги - то стоит
        /// Если перед солдатом союзники - повернуться к таргету
        /// Если перед солдатом пусто в сторону таргета - идти к нему
        /// Таргет для солдата обновляется раз в тактик апдейт

        Ray ray_forward = new Ray(transform.position + new Vector3(0, 2, 0), transform.forward);
        Ray ray_forwardl = new Ray(transform.position + transform.right * .9f + new Vector3(0, 2, 0) + .8f * transform.forward, transform.forward);
        Ray ray_forwardr = new Ray(transform.position - transform.right * .9f + new Vector3(0, 2, 0) + .8f * transform.forward, transform.forward);
        Ray R3 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f * 1 / 4));
        Ray R4 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f * 3 / 4));

        Ray R5 = new Ray(transform.position + new Vector3(0, 2, 0), -transform.right);
        Ray R6 = new Ray(transform.position + new Vector3(0, 2, 0), transform.right);

        //Debug.DrawLine(transform.position + new Vector3(0, 1, 0), transform.position + transform.forward);
        Debug.DrawLine(transform.position + transform.right * .9f + new Vector3(0, 2, 0) + .5f * transform.forward, transform.position + transform.right * .9f + new Vector3(0, 2, 0) + .5f * transform.forward - transform.forward);

        RaycastHit[] hit = new RaycastHit[7];
        bool temp = Physics.Raycast(ray_forward, out hit[0], 3.0f);
        temp = Physics.Raycast(ray_forwardl, out hit[1], 2.0f);
        temp = Physics.Raycast(ray_forwardr, out hit[2], 2.0f);
        temp = Physics.Raycast(R3, out hit[3], 0.1f);
        temp = Physics.Raycast(R4, out hit[4], 0.1f);
        temp = Physics.Raycast(R5, out hit[5], 0.3f);
        temp = Physics.Raycast(R6, out hit[6], 0.3f);

        /// Если впереди враг - солдат стоит
        for (int t = 0; t < 5; t++)
        {
            if (hit[t].transform != null)
                if (hit[t].transform.GetComponent<UnitSoldiers>() != null)
                {

                    if (hit[t].transform.GetComponent<UnitSoldiers>().command != command)
                    {
                        return; // Когда рядом враг, солдат стоит

                    }
                }
        }

        RaycastHit[] hitf = new RaycastHit[7];

        temp = Physics.Raycast(ray_forward, out hitf[0], 2.0f);
        temp = Physics.Raycast(ray_forward, out hitf[1], 2.0f);
        temp = Physics.Raycast(ray_forward, out hitf[2], 2.0f);
        temp = Physics.Raycast(ray_forward, out hitf[3], 2.0f);
        temp = Physics.Raycast(ray_forward, out hitf[4], 2.0f);
        temp = Physics.Raycast(R5, out hitf[5], 1f);
        temp = Physics.Raycast(R6, out hitf[6], 1f);

        // f - nobody there
        bool fr = false;

        for (int t = 0; t < 7; t++)
        {
            if (hitf[t].transform != null)
            {
                fr = true; break;
            }
        }
        if (hitf[5].transform == null && hitf[5].transform == null)
        {
            //transform.Translate(-Vector3.right * Time.deltaTime); 
        }
        if (hitf[6].transform == null && hitf[6].transform == null)
        {
            //transform.Translate(Vector3.right * Time.deltaTime); 
        }

        if (fr == true)
        {

        }
        else
        {
            /// Добавим, если сспереди и слева никого нет, то идем вбок
            if ((transform.forward - (nowTargetPosition - transform.position).normalized).magnitude > 0.01)
            {
                Remaketarget();
                Vector3 newDir = Vector3.RotateTowards(transform.forward, (nowTargetPosition - transform.position), 0.1f, 0.0F);
                transform.rotation = Quaternion.LookRotation(newDir);
            }
            else
            {
                transform.Translate(Vector3.forward * Time.deltaTime);
            }
        }
    }


    /// <summary>
    /// Кидаем рейкасты, и если находим вражеского солдата - бьём
    /// </summary>
    public void at()
    {
        // посмотрим во все стороны и если есть враг - атакуем
        RaycastHit hit;

        Ray ray1 = new Ray(transform.position + transform.right * 0.3f + new Vector3(0, 2, 0), transform.forward);
        bool temp = Physics.Raycast(ray1, out hit, 5.0f);
        if (hit.transform != null)
            if (hit.transform.GetComponent<UnitSoldiers>() != null)
            {
                if (hit.transform.GetComponent<UnitSoldiers>().command != command)
                {
                    myat(attack, hit.transform.GetComponent<UnitSoldiers>().defence, power,
                       hit.transform.GetComponent<UnitSoldiers>().numReg, hit.transform.GetComponent<UnitSoldiers>().numSoldier); return; // Когда рядом враг, солдат стоит
                }
            }

        Ray ray2 = new Ray(transform.position - transform.right * 0.3f + new Vector3(0, 2, 0), transform.forward);
        temp = Physics.Raycast(ray2, out hit, 5.0f);
        if (hit.transform != null)
            if (hit.transform.GetComponent<UnitSoldiers>() != null)
            {
                if (hit.transform.GetComponent<UnitSoldiers>().command != command)
                {
                    myat(attack, hit.transform.GetComponent<UnitSoldiers>().defence, power,
                       hit.transform.GetComponent<UnitSoldiers>().numReg, hit.transform.GetComponent<UnitSoldiers>().numSoldier); return; // Когда рядом враг, солдат стоит
                }
            }


        Ray ray3 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f / 4 * 7));
        temp = Physics.Raycast(ray3, out hit, 5.0f);
        if (hit.transform != null)
            if (hit.transform.GetComponent<UnitSoldiers>() != null)
            {
                if (hit.transform.GetComponent<UnitSoldiers>().command != command)
                {
                    myat(attack, hit.transform.GetComponent<UnitSoldiers>().defence, power,
                       hit.transform.GetComponent<UnitSoldiers>().numReg, hit.transform.GetComponent<UnitSoldiers>().numSoldier); return; // Когда рядом враг, солдат стоит
                }
            }


        Ray ray4 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f / 4));
        temp = Physics.Raycast(ray4, out hit, 5.0f);
        if (hit.transform != null)
            if (hit.transform.GetComponent<UnitSoldiers>() != null)
            {
                if (hit.transform.GetComponent<UnitSoldiers>().command != command)
                {
                    myat(attack, hit.transform.GetComponent<UnitSoldiers>().defence, power,
                        hit.transform.GetComponent<UnitSoldiers>().numReg, hit.transform.GetComponent<UnitSoldiers>().numSoldier); return; // Когда рядом враг, солдат стоит
                }
            }

        Ray ray5 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f / 4 * 7));
        temp = Physics.Raycast(ray5, out hit, 5.0f);
        if (hit.transform != null)
            if (hit.transform.GetComponent<UnitSoldiers>() != null)
            {
                if (hit.transform.GetComponent<UnitSoldiers>().command != command)
                {
                    myat(attack, hit.transform.GetComponent<UnitSoldiers>().defence, power,
                           hit.transform.GetComponent<UnitSoldiers>().numReg, hit.transform.GetComponent<UnitSoldiers>().numSoldier); return; // Когда рядом враг, солдат стоит
                }
            }

        Ray ray6 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f / 4 * 7.3f));
        temp = Physics.Raycast(ray6, out hit, 5.0f);
        if (hit.transform != null)
            if (hit.transform.GetComponent<UnitSoldiers>() != null)
            {
                if (hit.transform.GetComponent<UnitSoldiers>().command != command)
                {
                    myat(attack, hit.transform.GetComponent<UnitSoldiers>().defence, power,
                        hit.transform.GetComponent<UnitSoldiers>().numReg, hit.transform.GetComponent<UnitSoldiers>().numSoldier); return; // Когда рядом враг, солдат стоит
                }
            }

        Ray ray7 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f / 6));
        temp = Physics.Raycast(ray7, out hit, 5.0f);
        if (hit.transform != null)
            if (hit.transform.GetComponent<UnitSoldiers>() != null)
            {
                if (hit.transform.GetComponent<UnitSoldiers>().command != command)
                {
                    myat(attack, hit.transform.GetComponent<UnitSoldiers>().defence, power,
                      hit.transform.GetComponent<UnitSoldiers>().numReg, hit.transform.GetComponent<UnitSoldiers>().numSoldier); return; // Когда рядом враг, солдат стоит
                }
            }
    }


    /// <summary>
    /// Происходит расчет, убит ли враг при такой атаке и защите
    /// </summary>
    /// <param name="at"></param>
    /// <param name="def"></param>
    /// <param name="m"></param>
    /// <param name="enemyNumReg"></param>
    /// <param name="enemyNumSol"></param>
    /// <returns></returns>
    public bool myat(float at, float def, float m, int enemyNumReg, int enemyNumSol)
    {
        if (Random.Range(0, 20) < at)
        {
            if (Random.Range(0, 20) > (def - m))
            {
                DeathBuf r;
                r = new DeathBuf();
                r.regNum = enemyNumReg;
                r.solNum = enemyNumSol;
                r.time = Random.Range(0, 1000) / 1000.0f * mainStatic.TACTICPERIOD;
                DeadBuff.put(r);

                return true; // убит
            }
        }
        return false; // жив
    }


    /// <summary>
    /// Находим в списке целей в бою ближайший
    /// </summary>
    public void Remaketarget()
    {
        float temp = 99999;
        Vector3 t = new Vector3(0, 0, 0);
        for (int i = 0; i < mainStatic.regiment_stat[numReg].targPos.Count; i++)
        {
            if (((transform.position - mainStatic.regiment_stat[numReg].targPos[i]).magnitude) < temp)
            {
                temp = (transform.position - mainStatic.regiment_stat[numReg].targPos[i]).magnitude;
                t = mainStatic.regiment_stat[numReg].targPos[i];
            }
        }
        nowTargetPosition = t;
        nowTargetPosition.y = transform.position.y;
    }
}